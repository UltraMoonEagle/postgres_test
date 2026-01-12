/*-------------------------------------------------------------------------
 *
 * blockchain_phantom.c
 *	  Phantom blocks persistence for blockchain tables
 *
 * Stores metadata for allocated-but-uncommitted blockchain counters to ensure
 * cryptographic chain verification remains possible after rollbacks/restarts.
 *
 * IDENTIFICATION
 *	  src/backend/access/blockchain/blockchain_phantom.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/xact.h"
#include "executor/spi.h"
#include "utils/builtins.h"
#include "utils/timestamp.h"
#include "catalog/pg_type.h"

/*
 * blockchain_persist_phantom_block
 *
 * Persists metadata for an allocated blockchain counter so the hash chain
 * can be reconstructed even if the transaction rolls back.
 *
 * This function is called immediately after counter allocation and hash
 * computation, before the transaction commits.
 */
void
blockchain_persist_phantom_block(Oid table_oid, uint64 counter,
                                 const unsigned char *prev_hash,
                                 const unsigned char *computed_hash)
{
    int spi_result;
    char query[2048];
    Datum values[7];
    char nulls[7];
    Oid arg_types[7];

    /* Connect to SPI */
    spi_result = SPI_connect();
    if (spi_result != SPI_OK_CONNECT)
    {
        elog(WARNING, "blockchain_persist_phantom_block: SPI_connect failed");
        return;  /* Non-fatal - cache will still work */
    }

    PG_TRY();
    {
        /* Convert binary hashes to bytea format for storage */
        bytea *prev_hash_bytea = (bytea *) palloc(VARHDRSZ + 32);
        SET_VARSIZE(prev_hash_bytea, VARHDRSZ + 32);
        memcpy(VARDATA(prev_hash_bytea), prev_hash, 32);

        bytea *computed_hash_bytea = (bytea *) palloc(VARHDRSZ + 32);
        SET_VARSIZE(computed_hash_bytea, VARHDRSZ + 32);
        memcpy(VARDATA(computed_hash_bytea), computed_hash, 32);

        /* Prepare parameters */
        arg_types[0] = OIDOID;      /* table_oid */
        arg_types[1] = INT8OID;     /* counter */
        arg_types[2] = BYTEAOID;    /* prev_hash */
        arg_types[3] = BYTEAOID;    /* computed_hash */
        arg_types[4] = TIMESTAMPTZOID; /* allocated_at */
        arg_types[5] = XIDOID;      /* xid */
        arg_types[6] = TEXTOID;     /* status */

        values[0] = ObjectIdGetDatum(table_oid);
        values[1] = Int64GetDatum((int64) counter);
        values[2] = PointerGetDatum(prev_hash_bytea);
        values[3] = PointerGetDatum(computed_hash_bytea);
        values[4] = TimestampTzGetDatum(GetCurrentTimestamp());
        values[5] = TransactionIdGetDatum(GetCurrentTransactionId());
        values[6] = CStringGetTextDatum("ALLOCATED");

        memset(nulls, ' ', sizeof(nulls));

        /* Use INSERT ... ON CONFLICT to handle concurrent inserts */
        snprintf(query, sizeof(query),
                "INSERT INTO pg_catalog.blockchain_phantom_blocks "
                "(table_oid, counter, prev_hash, computed_hash, allocated_at, xid, status) "
                "VALUES ($1, $2, $3, $4, $5, $6, $7) "
                "ON CONFLICT (table_oid, counter) DO UPDATE SET "
                "prev_hash = EXCLUDED.prev_hash, "
                "computed_hash = EXCLUDED.computed_hash, "
                "allocated_at = EXCLUDED.allocated_at, "
                "xid = EXCLUDED.xid, "
                "status = EXCLUDED.status");

        spi_result = SPI_execute_with_args(query, 7, arg_types, values, nulls, false, 0);

        if (spi_result != SPI_OK_INSERT && spi_result != SPI_OK_INSERT_RETURNING)
        {
            elog(WARNING, "blockchain_persist_phantom_block: INSERT failed for table %u, counter %llu",
                 table_oid, (unsigned long long) counter);
        }
        else
        {
            elog(DEBUG1, "Persisted phantom block: table_oid=%u, counter=%llu, xid=%u",
                 table_oid, (unsigned long long) counter, GetCurrentTransactionId());
        }

        SPI_finish();
    }
    PG_CATCH();
    {
        SPI_finish();
        /* Log but don't propagate - phantom block persistence is best-effort */
        elog(WARNING, "blockchain_persist_phantom_block: exception caught, continuing");
        FlushErrorState();
    }
    PG_END_TRY();
}

/*
 * blockchain_mark_phantom_committed
 *
 * Marks a phantom block as committed (can be cleaned up later).
 * Called from transaction commit hook.
 */
void
blockchain_mark_phantom_committed(Oid table_oid, uint64 counter)
{
    int spi_result;
    char query[512];
    Datum values[2];
    char nulls[2];
    Oid arg_types[2];

    spi_result = SPI_connect();
    if (spi_result != SPI_OK_CONNECT)
        return;

    PG_TRY();
    {
        arg_types[0] = OIDOID;
        arg_types[1] = INT8OID;
        values[0] = ObjectIdGetDatum(table_oid);
        values[1] = Int64GetDatum((int64) counter);
        memset(nulls, ' ', sizeof(nulls));

        snprintf(query, sizeof(query),
                "UPDATE pg_catalog.blockchain_phantom_blocks "
                "SET status = 'COMMITTED' "
                "WHERE table_oid = $1 AND counter = $2");

        spi_result = SPI_execute_with_args(query, 2, arg_types, values, nulls, false, 0);

        SPI_finish();
    }
    PG_CATCH();
    {
        SPI_finish();
        FlushErrorState();
    }
    PG_END_TRY();
}

/*
 * blockchain_mark_phantom_aborted
 *
 * Marks a phantom block as aborted.
 * Called from transaction abort hook.
 */
void
blockchain_mark_phantom_aborted(Oid table_oid, uint64 counter)
{
    int spi_result;
    char query[512];
    Datum values[2];
    char nulls[2];
    Oid arg_types[2];

    spi_result = SPI_connect();
    if (spi_result != SPI_OK_CONNECT)
        return;

    PG_TRY();
    {
        arg_types[0] = OIDOID;
        arg_types[1] = INT8OID;
        values[0] = ObjectIdGetDatum(table_oid);
        values[1] = Int64GetDatum((int64) counter);
        memset(nulls, ' ', sizeof(nulls));

        snprintf(query, sizeof(query),
                "UPDATE pg_catalog.blockchain_phantom_blocks "
                "SET status = 'ABORTED' "
                "WHERE table_oid = $1 AND counter = $2");

        spi_result = SPI_execute_with_args(query, 2, arg_types, values, nulls, false, 0);

        SPI_finish();
    }
    PG_CATCH();
    {
        SPI_finish();
        FlushErrorState();
    }
    PG_END_TRY();
}

/*
 * blockchain_cleanup_phantom_blocks
 *
 * Removes old committed phantom blocks (the actual row exists, so we don't
 * need the phantom metadata anymore).
 *
 * Should be called periodically or during VACUUM.
 */
void
blockchain_cleanup_phantom_blocks(Oid table_oid)
{
    int spi_result;
    char query[512];

    spi_result = SPI_connect();
    if (spi_result != SPI_OK_CONNECT)
        return;

    PG_TRY();
    {
        /* Remove committed phantom blocks older than 1 day */
        snprintf(query, sizeof(query),
                "DELETE FROM pg_catalog.blockchain_phantom_blocks "
                "WHERE table_oid = %u "
                "AND status = 'COMMITTED' "
                "AND allocated_at < NOW() - INTERVAL '1 day'",
                table_oid);

        SPI_execute(query, false, 0);

        elog(DEBUG1, "Cleaned up old phantom blocks for table %u", table_oid);

        SPI_finish();
    }
    PG_CATCH();
    {
        SPI_finish();
        FlushErrorState();
    }
    PG_END_TRY();
}
