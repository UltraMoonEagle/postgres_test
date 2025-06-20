// File: src/backend/commands/blockchain_views.c
// Auto-generates user/audit views and a verify function for blockchain tables

#include "postgres.h"
#include "access/relation.h"
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
#include "executor/spi.h"
#include "miscadmin.h"
#include "tcop/utility.h"

static void
CreateBlockchainViewsInternal(const char *relname, Oid relnamespace)
{
    StringInfoData user_view, audit_view, verify_func, user_cols;
    int attno;
    Relation rel;
    TupleDesc tupdesc;

    const char *nspname = get_namespace_name(relnamespace);

    RangeVar *target_rv = makeRangeVar(get_namespace_name(relnamespace), relname, -1);
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


// Hook into DefineRelation
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

// Datum
// verify_chain_internal(PG_FUNCTION_ARGS)
// {
//     Oid relid = PG_GETARG_OID(0);
//     Datum row_id = PG_GETARG_DATUM(1);
//     Relation rel = relation_open(relid, AccessShareLock);
//     TupleDesc tupdesc = RelationGetDescr(rel);

//     char *relname = RelationGetRelationName(rel);
//     char *nspname = get_namespace_name(RelationGetNamespace(rel));

//     StringInfoData query;
//     initStringInfo(&query);
//     appendStringInfo(&query,
//         "SELECT * FROM %s.%s WHERE __row_id = $1 ORDER BY __tx_lsn",
//         quote_identifier(nspname), quote_identifier(relname));

//     SPI_connect();
//     Oid argtypes[1] = {UUIDOID};
//     Datum values[1] = {row_id};
//     const char *nulls = " ";

//     SPI_execute_with_args(query.data, 1, argtypes, values, &nulls, false, 0);

//     if (SPI_processed == 0)
//     {
//         SPI_finish();
//         relation_close(rel, AccessShareLock);
//         PG_RETURN_TEXT_P(cstring_to_text("No such row_id found."));
//     }

//     bytea *expected_prev = NULL;
//     bool valid = true;

//     for (uint64 i = 0; i < SPI_processed; i++)
//     {
//         HeapTuple tuple = SPI_tuptable->vals[i];
//         TupleTableSlot *slot = MakeSingleTupleTableSlot(tupdesc, &TTSOpsMinimalTuple);
//         ExecStoreTuple(tuple, slot->tts_tuple, false);
//         ExecMaterializeSlot(slot);

//         Datum d_curr = slot_getattr(slot, SPI_fnumber(SPI_tuptable->tupdesc, "__curr_hash"), NULL);
//         Datum d_prev = slot_getattr(slot, SPI_fnumber(SPI_tuptable->tupdesc, "__prev_hash"), NULL);
//         Datum d_lsn  = slot_getattr(slot, SPI_fnumber(SPI_tuptable->tupdesc, "__tx_lsn"), NULL);
//         Datum d_ts   = slot_getattr(slot, SPI_fnumber(SPI_tuptable->tupdesc, "__tx_timestamp"), NULL);

//         bytea *curr_stored = (bytea *) DatumGetPointer(d_curr);
//         bytea *prev_stored = (bytea *) DatumGetPointer(d_prev);
//         XLogRecPtr lsn = DatumGetLSN(d_lsn);
//         TimestampTz ts = DatumGetTimestampTz(d_ts);

//         bytea *computed = compute_curr_hash(rel, slot, ts, expected_prev, lsn);

//         if (VARSIZE(computed) != VARSIZE(curr_stored) ||
//             memcmp(VARDATA(computed), VARDATA(curr_stored), VARSIZE_ANY_EXHDR(computed)) != 0)
//         {
//             valid = false;
//             ExecDropSingleTupleTableSlot(slot);
//             pfree(computed);
//             break;
//         }

//         expected_prev = curr_stored;
//         ExecDropSingleTupleTableSlot(slot);
//         pfree(computed);
//     }

//     SPI_finish();
//     relation_close(rel, AccessShareLock);

//     if (valid)
//         PG_RETURN_TEXT_P(cstring_to_text("Chain is valid."));
//     else
//         PG_RETURN_TEXT_P(cstring_to_text("Chain integrity failed!"));
// }
