#include "postgres.h"
#include "fmgr.h"
#include "executor/spi.h"
#include "access/xact.h"
#include "utils/timestamp.h"
#include "utils/builtins.h"
#include "utils/uuid.h"
#include "utils/pg_lsn.h"
#include "utils/memutils.h"
#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "miscadmin.h"
#include "utils/lsyscache.h"

#include "blockchain/blockchain_hash.h" 

Datum anchor_bc_query(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(anchor_bc_query);

static bytea *hash_query_result(SPITupleTable *tuptable, uint64 nrows);
Datum
anchor_bc_query(PG_FUNCTION_ARGS)
{
    const char *target_table = text_to_cstring(PG_GETARG_TEXT_PP(0));
    const char *query = text_to_cstring(PG_GETARG_TEXT_PP(1));
    bytea *result_hash = NULL;

    StringInfoData sql;
    initStringInfo(&sql);

    PG_TRY();
    {
        int spi_ret = SPI_connect();
        if (spi_ret != SPI_OK_CONNECT)
            elog(ERROR, "SPI_connect failed: %d", spi_ret);

        spi_ret = SPI_execute(query, true, 0);
        if (spi_ret != SPI_OK_SELECT)
            elog(ERROR, "SPI_execute failed: %d", spi_ret);

        if (SPI_tuptable == NULL || SPI_processed == 0)
            elog(ERROR, "Query returned no rows");

        result_hash = hash_query_result(SPI_tuptable, SPI_processed);

        SPI_finish();
    }
    PG_CATCH();
    {
        /* Clean up SPI if connected */
            SPI_finish();
        ereport(ERROR,
                (errmsg("Error in anchor_bc_query during query hash computation")));
    }
    PG_END_TRY();

    appendStringInfo(&sql,
        "INSERT INTO %s(anchor_hash) VALUES ($1);", target_table);

    PG_TRY();
    {
        int spi_ret = SPI_connect();
        if (spi_ret != SPI_OK_CONNECT)
            elog(ERROR, "SPI_connect (for insert) failed: %d", spi_ret);

        Oid argtypes[1] = { BYTEAOID };
        Datum values[1] = { PointerGetDatum(result_hash) };
        char nulls[1] = { ' ' };

        spi_ret = SPI_execute_with_args(sql.data, 1, argtypes, values, nulls, false, 0);
        if (spi_ret != SPI_OK_INSERT)
            elog(ERROR, "SPI insert failed: %d", spi_ret);

        SPI_finish();
    }
    PG_CATCH();
    {
        /* Clean up SPI if connected */
            SPI_finish();
        ereport(ERROR,
                (errmsg("Error in anchor_bc_query during blockchain insert")));
    }
    PG_END_TRY();

    // Let PG memory context clean up result_hash and sql automatically
    PG_RETURN_VOID();
}

static bytea *
hash_query_result(SPITupleTable *tuptable, uint64 nrows)
{
    bc_sha256_ctx ctx;
    bc_sha256_init(&ctx);

    TupleDesc tupdesc = tuptable->tupdesc;

    for (uint64 i = 0; i < nrows; i++)
    {
        HeapTuple tuple = tuptable->vals[i];
        for (int j = 0; j < tupdesc->natts; j++)
        {
            Form_pg_attribute attr = TupleDescAttr(tupdesc, j);
            if (attr->attisdropped)
                continue;

            bool isnull;
            Datum val = heap_getattr(tuple, j + 1, tupdesc, &isnull);

            if (!isnull)
            {
                Oid typoutput;
                bool typisvarlena;
                char *str;

                getTypeOutputInfo(attr->atttypid, &typoutput, &typisvarlena);
                str = OidOutputFunctionCall(typoutput, val);
                bc_sha256_update(&ctx, (const uint8 *) str, strlen(str));
                pfree(str);
            }
            else
            {
                bc_sha256_update(&ctx, (const uint8 *) "<NULL>", 6);
            }
        }
    }

    bytea *result = (bytea *) palloc(VARHDRSZ + BC_SHA256_DIGEST_LENGTH);
    SET_VARSIZE(result, VARHDRSZ + BC_SHA256_DIGEST_LENGTH);
    bc_sha256_final(&ctx, (uint8 *) VARDATA(result));
    return result;
}
