// File: src/backend/commands/blockchain_views.c
// Auto-generates user/audit views and a verify function for blockchain tables

#include "postgres.h"
#include "access/relation.h"
#include "access/table.h"
#include "access/sdir.h"
#include "access/heapam.h"
#include "access/tableam.h"
#include "commands/defrem.h"
#include "commands/blockchain_view.h"
#include "catalog/namespace.h"
#include "catalog/pg_class.h"
#include "catalog/pg_attribute.h"
#include "lib/stringinfo.h"
#include "nodes/parsenodes.h"
#include "nodes/pg_list.h"
#include "nodes/makefuncs.h"
#include "utils/lsyscache.h"
#include "utils/builtins.h"
#include "utils/rel.h"
#include "utils/uuid.h"
#include "utils/ruleutils.h"
#include "utils/varlena.h"
#include "utils/snapmgr.h"
#include "utils/pg_lsn.h"
#include "utils/timestamp.h"
#include "executor/spi.h"
#include "executor/tuptable.h"
#include "miscadmin.h"
#include "tcop/utility.h"

#include "blockchain/blockchain_hash.h"
#include "blockchain/blockchainam.h"

PG_FUNCTION_INFO_V1(verify_blockchain_chain);

static bytea *recompute_hash(Relation rel, HeapTuple tuple, bytea *prev_hash, XLogRecPtr tx_lsn, TimestampTz ts);
static bytea *recompute_hash_with_counter(Relation rel, HeapTuple tuple, bytea *prev_hash, uint64 counter, TimestampTz ts);

static void
CreateBlockchainViewsInternal(const char *relname, Oid relnamespace)
{
    StringInfoData user_view, audit_view, verify_func, user_cols;
    int attno;
    Relation rel;
    TupleDesc tupdesc;

    const char *nspname = get_namespace_name(relnamespace);

    RangeVar *target_rv = makeRangeVar(pstrdup(nspname), pstrdup(relname), -1);
    rel = relation_openrv(target_rv, AccessShareLock);
    tupdesc = RelationGetDescr(rel);

    initStringInfo(&user_cols);
    for (attno = 0; attno < tupdesc->natts; attno++)
    {
        Form_pg_attribute attr = TupleDescAttr(tupdesc, attno);
        if (attr->attisdropped || strncmp(NameStr(attr->attname), "__", 2) == 0)
            continue;

        appendStringInfo(&user_cols, "%s%s",
                         user_cols.len > 0 ? ", " : "",
                         quote_identifier(NameStr(attr->attname)));
    }

    initStringInfo(&user_view);
    appendStringInfo(&user_view,
                     "CREATE OR REPLACE VIEW %s.user_view_%s AS "
                     "SELECT %s FROM %s.%s WHERE __is_latest = true",
                     quote_identifier(nspname), quote_identifier(relname),
                     user_cols.data,
                     quote_identifier(nspname), quote_identifier(relname));

    initStringInfo(&audit_view);
    appendStringInfo(&audit_view,
                     "CREATE OR REPLACE VIEW %s.audit_view_%s AS "
                     "SELECT __row_id, __tx_version, __tx_type, __tx_origin, __is_latest, "
                     "__tx_lsn, encode(__prev_hash, 'hex') AS prev_hash, "
                     "encode(__curr_hash, 'hex') AS curr_hash, __tx_timestamp "
                     "FROM %s.%s WHERE __is_latest = true ORDER BY __tx_lsn",
                     quote_identifier(nspname), quote_identifier(relname),
                     quote_identifier(nspname), quote_identifier(relname));

    initStringInfo(&verify_func);
    appendStringInfo(&verify_func,
                     "CREATE OR REPLACE FUNCTION %s.verify_chain_%s(uuid) RETURNS json AS $$\n"
                     "BEGIN\n"
                     "RETURN (SELECT json_agg(json_build_object(\n"
                     " 'tx_version', __tx_version,\n"
                     " 'tx_type', __tx_type,\n"
                     " 'tx_lsn', __tx_lsn,\n"
                     " 'tx_timestamp', __tx_timestamp,\n"
                     " 'prev_hash', encode(__prev_hash, 'hex'),\n"
                     " 'curr_hash', encode(__curr_hash, 'hex')\n"
                     ")) FROM %s.%s WHERE __row_id = $1);\n"
                     "END;\n"
                     "$$ LANGUAGE plpgsql;",
                     quote_identifier(nspname), quote_identifier(relname),
                     quote_identifier(nspname), quote_identifier(relname));

    relation_close(rel, AccessShareLock);

    PG_TRY();
    {
        if (SPI_connect() != SPI_OK_CONNECT)
            elog(ERROR, "SPI_connect failed");

        if (SPI_execute(user_view.data, false, 0) != SPI_OK_UTILITY)
            elog(ERROR, "Failed to create user view");

        if (SPI_execute(audit_view.data, false, 0) != SPI_OK_UTILITY)
            elog(ERROR, "Failed to create audit view");

        if (SPI_execute(verify_func.data, false, 0) != SPI_OK_UTILITY)
            elog(ERROR, "Failed to create verify function");

        SPI_finish();
    }
    PG_CATCH();
    {
        SPI_finish();
        PG_RE_THROW();
    }
    PG_END_TRY();

    pfree(user_cols.data);
    pfree(user_view.data);
    pfree(audit_view.data);
    pfree(verify_func.data);
}

void
MaybeCreateBlockchainViews(CreateStmt *stmt, Oid relid)
{
    if (stmt->accessMethod && strcmp(stmt->accessMethod, "blockchain") == 0)
    {
        const char *relname = stmt->relation->relname;
        Oid relnamespace = RangeVarGetCreationNamespace(stmt->relation);
        CreateBlockchainViewsInternal(relname, relnamespace);
    }
}

Datum
verify_blockchain_chain(PG_FUNCTION_ARGS)
{
    text *relname_text = PG_GETARG_TEXT_P(0);
    Relation rel;
    TableScanDesc scan;
    HeapTuple tuple;
    TupleDesc tupdesc;
    bytea *last_curr_hash = NULL;
    bool valid = true;

    rel = table_openrv(makeRangeVarFromNameList(textToQualifiedNameList(relname_text)), AccessShareLock);
    tupdesc = RelationGetDescr(rel);
    scan = table_beginscan_strat(rel, GetActiveSnapshot(), 0, NULL, true, true);

    while ((tuple = heap_getnext(scan, ForwardScanDirection)) != NULL)
    {
        bool isnull;
    
        Datum d_latest = heap_getattr(tuple, get_attnum(RelationGetRelid(rel), "__is_latest"), tupdesc, &isnull);
        if (isnull || !DatumGetBool(d_latest))
            continue; 

        Datum d_prev = heap_getattr(tuple, get_attnum(RelationGetRelid(rel), "__prev_hash"), tupdesc, &isnull);
        if (isnull){
            elog(ERROR, "__prev_hash column is NULL");
            continue;
        }
        bytea *prev_hash_stored = DatumGetByteaP(d_prev);

        Datum d_curr = heap_getattr(tuple, get_attnum(RelationGetRelid(rel), "__curr_hash"), tupdesc, &isnull);
        if (isnull)
        {
            elog(WARNING, "Skipping row with NULL __curr_hash");
            continue;
        }
           
        bytea *curr_hash_stored = DatumGetByteaP(d_curr);

        Datum d_lsn = heap_getattr(tuple, get_attnum(RelationGetRelid(rel), "__tx_lsn"), tupdesc, &isnull);
        if (isnull)
            elog(ERROR, "__tx_lsn column is NULL");
        /* Now it's a counter (int8) instead of LSN */
        uint64 counter = DatumGetInt64(d_lsn);

        Datum d_ts = heap_getattr(tuple, get_attnum(RelationGetRelid(rel), "__tx_timestamp"), tupdesc, &isnull);
        if (isnull)
            elog(ERROR, "__tx_timestamp column is NULL");
        TimestampTz tx_ts = DatumGetTimestampTz(d_ts);

        bytea *recomputed = recompute_hash_with_counter(rel, tuple, last_curr_hash, counter, tx_ts);

        bool curr_mismatch = (VARSIZE_ANY_EXHDR(curr_hash_stored) != VARSIZE_ANY_EXHDR(recomputed) ||
            memcmp(VARDATA_ANY(curr_hash_stored), VARDATA_ANY(recomputed), VARSIZE_ANY_EXHDR(curr_hash_stored)) != 0);

        bool prev_mismatch = 
            (last_curr_hash && (VARSIZE_ANY_EXHDR(prev_hash_stored) != VARSIZE_ANY_EXHDR(last_curr_hash) ||
             memcmp(VARDATA_ANY(prev_hash_stored), VARDATA_ANY(last_curr_hash), VARSIZE_ANY_EXHDR(prev_hash_stored)) != 0));
        
        if (curr_mismatch || prev_mismatch)
        {
        // Try to extract row ID (if available)
        Datum d_id = heap_getattr(tuple, 1, tupdesc, &isnull); // Assuming user column is first
        int id = isnull ? -1 : DatumGetInt32(d_id);

        char *stored_hex = DatumGetCString(DirectFunctionCall1(byteaout, PointerGetDatum(curr_hash_stored)));
        char *recomputed_hex = DatumGetCString(DirectFunctionCall1(byteaout, PointerGetDatum(recomputed)));

        elog(WARNING, "Hash mismatch at id = %d", id);
        elog(WARNING, "  stored curr_hash:     %s", stored_hex);
        elog(WARNING, "  recomputed curr_hash: %s", recomputed_hex);

        if (last_curr_hash)
        {
            char *last_hex = DatumGetCString(DirectFunctionCall1(byteaout, PointerGetDatum(last_curr_hash)));
            char *prev_hex = DatumGetCString(DirectFunctionCall1(byteaout, PointerGetDatum(prev_hash_stored)));
            elog(WARNING, "  prev_hash stored:     %s", prev_hex);
            elog(WARNING, "  expected prev_hash:   %s", last_hex);
            pfree(last_hex);
            pfree(prev_hex);
        }

        pfree(stored_hex);
        pfree(recomputed_hex);
        pfree(recomputed);

        valid = false;
        break;
        }

        if (last_curr_hash)
            pfree(last_curr_hash);
        last_curr_hash = recomputed;
        }

    table_endscan(scan);
    table_close(rel, AccessShareLock);
    if (last_curr_hash)
        pfree(last_curr_hash);

    PG_RETURN_BOOL(valid);
}

static bytea *
recompute_hash(Relation rel, HeapTuple tuple, bytea *prev_hash, XLogRecPtr tx_lsn, TimestampTz ts)
{
    TupleDesc tupdesc = RelationGetDescr(rel);
    bc_sha256_ctx ctx;
    uint8 hash_output[BC_SHA256_DIGEST_LENGTH];
    bytea *result;
    char lsnbuf[64];

    memset(&ctx, 0, sizeof(ctx));
    bc_sha256_init(&ctx);

    elog(WARNING, "---- Begin Hash Input Debug ----");

    /* 1. Previous hash */
    if (prev_hash && VARSIZE_ANY_EXHDR(prev_hash) > 0)
    {
        elog(WARNING, "prev_hash: %s", DatumGetCString(DirectFunctionCall1(byteaout, PointerGetDatum(prev_hash))));
        bc_sha256_update(&ctx, (uint8 *) VARDATA_ANY(prev_hash),
                         VARSIZE_ANY_EXHDR(prev_hash));
    }
    else
    {
        elog(WARNING, "prev_hash: (null), using zero hash");
        static const uint8 zero_hash[BC_SHA256_DIGEST_LENGTH] = {0};
        bc_sha256_update(&ctx, zero_hash, BC_SHA256_DIGEST_LENGTH);
    }

    /* 2. LSN */
    snprintf(lsnbuf, sizeof(lsnbuf), "%X/%X", (uint32)(tx_lsn >> 32), (uint32) tx_lsn);
    elog(WARNING, "tx_lsn: %s", lsnbuf);
    bc_sha256_update(&ctx, (const uint8 *) lsnbuf, strlen(lsnbuf));

    /* 3. Timestamp (formatted as ISO 8601 UTC) */
    char tsbuf[64];
    struct pg_tm tm;
    fsec_t fsec;
    const char *tzn;

    if (timestamp2tm(ts, NULL, &tm, &fsec, &tzn, NULL) == 0)
    {
        snprintf(tsbuf, sizeof(tsbuf),
                 "%04d-%02d-%02dT%02d:%02d:%02d.%06dZ",
                 tm.tm_year, tm.tm_mon, tm.tm_mday,
                 tm.tm_hour, tm.tm_min, tm.tm_sec, (int) fsec);
        elog(WARNING, "tx_timestamp: %s", tsbuf);
        bc_sha256_update(&ctx, (const uint8 *) tsbuf, strlen(tsbuf));
    }
    else
    {
        elog(ERROR, "Failed to format timestamp for hashing");
    }

    /* 4. User-defined columns ONLY (exclude blockchain columns) */
    int user_natts = tupdesc->natts - NUM_BLOCKCHAIN_COLUMNS;
    for (int i = 0; i < user_natts; i++)
    {
        Form_pg_attribute attr = TupleDescAttr(tupdesc, i);
        if (attr->attisdropped)
            continue;

        Datum val;
        bool isnull;
        val = heap_getattr(tuple, attr->attnum, tupdesc, &isnull);

        if (!isnull)
        {
            Oid typoutput;
            bool typisvarlena;
            getTypeOutputInfo(attr->atttypid, &typoutput, &typisvarlena);
            char *outputstr = OidOutputFunctionCall(typoutput, val);
            elog(WARNING, "user column \"%s\": %s", NameStr(attr->attname), outputstr);
            bc_sha256_update(&ctx, (const uint8 *) outputstr, strlen(outputstr));
            pfree(outputstr);
        }
        else
        {
            elog(WARNING, "user column \"%s\": (null)", NameStr(attr->attname));
        }
    }

    /* Finalize the hash */
    bc_sha256_final(&ctx, hash_output);

    /* Emit final hash for visibility */
    char hexbuf[BC_SHA256_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < BC_SHA256_DIGEST_LENGTH; i++)
        sprintf(&hexbuf[i * 2], "%02x", ((unsigned char *) hash_output)[i]);
    elog(WARNING, "final recomputed hash: \\x%s", hexbuf);
    elog(WARNING, "---- End Hash Input Debug ----");

    result = (bytea *) MemoryContextAlloc(CurrentMemoryContext, VARHDRSZ + BC_SHA256_DIGEST_LENGTH);
    SET_VARSIZE(result, VARHDRSZ + BC_SHA256_DIGEST_LENGTH);
    memcpy(VARDATA(result), hash_output, BC_SHA256_DIGEST_LENGTH);

    return result;
}

/*
 * recompute_hash_with_counter - Recompute hash using counter instead of LSN
 *
 * This is the counter-based version of recompute_hash, used for verification
 * when the blockchain table uses global counters instead of LSNs.
 */
static bytea *
recompute_hash_with_counter(Relation rel, HeapTuple tuple, bytea *prev_hash, uint64 counter, TimestampTz ts)
{
    TupleDesc tupdesc = RelationGetDescr(rel);
    bc_sha256_ctx ctx;
    uint8 hash_output[BC_SHA256_DIGEST_LENGTH];
    bytea *result;
    char counterbuf[32];

    memset(&ctx, 0, sizeof(ctx));
    bc_sha256_init(&ctx);

    elog(DEBUG1, "---- Begin Hash Recomputation (Counter) ----");

    /* 1. Previous hash */
    if (prev_hash && VARSIZE_ANY_EXHDR(prev_hash) > 0)
    {
        elog(DEBUG2, "prev_hash exists, size: %d", VARSIZE_ANY_EXHDR(prev_hash));
        bc_sha256_update(&ctx, (uint8 *) VARDATA_ANY(prev_hash),
                         VARSIZE_ANY_EXHDR(prev_hash));
    }
    else
    {
        elog(DEBUG2, "prev_hash: (null), using zero hash");
        static const uint8 zero_hash[BC_SHA256_DIGEST_LENGTH] = {0};
        bc_sha256_update(&ctx, zero_hash, BC_SHA256_DIGEST_LENGTH);
    }

    /* 2. Counter (instead of LSN) */
    snprintf(counterbuf, sizeof(counterbuf), "%lu", counter);
    elog(DEBUG2, "counter: %s", counterbuf);
    bc_sha256_update(&ctx, (const uint8 *) counterbuf, strlen(counterbuf));

    /* 3. Timestamp (formatted as ISO 8601 UTC) */
    char tsbuf[64];
    struct pg_tm tm;
    fsec_t fsec;
    const char *tzn;

    if (timestamp2tm(ts, NULL, &tm, &fsec, &tzn, NULL) == 0)
    {
        snprintf(tsbuf, sizeof(tsbuf),
                 "%04d-%02d-%02dT%02d:%02d:%02d.%06dZ",
                 tm.tm_year, tm.tm_mon, tm.tm_mday,
                 tm.tm_hour, tm.tm_min, tm.tm_sec, (int) fsec);
        elog(DEBUG2, "tx_timestamp: %s", tsbuf);
        bc_sha256_update(&ctx, (const uint8 *) tsbuf, strlen(tsbuf));
    }
    else
    {
        elog(ERROR, "Failed to format timestamp for hashing");
    }

    /* 4. User-defined columns ONLY (exclude blockchain columns) */
    int user_natts = tupdesc->natts - NUM_BLOCKCHAIN_COLUMNS;
    for (int i = 0; i < user_natts; i++)
    {
        Form_pg_attribute attr = TupleDescAttr(tupdesc, i);
        if (attr->attisdropped)
            continue;

        Datum val;
        bool isnull;
        val = heap_getattr(tuple, attr->attnum, tupdesc, &isnull);

        if (!isnull)
        {
            Oid typoutput;
            bool typisvarlena;
            getTypeOutputInfo(attr->atttypid, &typoutput, &typisvarlena);
            char *outputstr = OidOutputFunctionCall(typoutput, val);
            elog(DEBUG3, "user column \"%s\": %s", NameStr(attr->attname), outputstr);
            bc_sha256_update(&ctx, (const uint8 *) outputstr, strlen(outputstr));
            pfree(outputstr);
        }
        else
        {
            elog(DEBUG3, "user column \"%s\": (null)", NameStr(attr->attname));
        }
    }

    /* Finalize the hash */
    bc_sha256_final(&ctx, hash_output);

    elog(DEBUG1, "---- End Hash Recomputation (Counter) ----");

    result = (bytea *) MemoryContextAlloc(CurrentMemoryContext, VARHDRSZ + BC_SHA256_DIGEST_LENGTH);
    SET_VARSIZE(result, VARHDRSZ + BC_SHA256_DIGEST_LENGTH);
    memcpy(VARDATA(result), hash_output, BC_SHA256_DIGEST_LENGTH);

    return result;
}
