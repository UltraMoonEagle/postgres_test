/*

Blocking ALTER and TRUNCATE have been enforced manually in lower level tablecmds.c 
Extend this for AUDITING purposes

*/


// #include "postgres.h"
// #include "tcop/utility.h"
// #include "catalog/pg_class.h"
// #include "catalog/namespace.h"
// #include "utils/rel.h"
// #include "utils/lsyscache.h"
// #include "commands/defrem.h"
// #include "access/htup_details.h"
// #include "access/table.h"
// #include "access/xact.h"
// #include "nodes/parsenodes.h"
// #include "commands/tablecmds.h"
// #include "commands/dbcommands.h"
// #include "catalog/objectaccess.h"
// #include "miscadmin.h"

// static ProcessUtility_hook_type prev_ProcessUtility_hook = NULL;

// static void
// blockchain_ProcessUtility(PlannedStmt *pstmt,
//                           const char *queryString,
//                           bool readOnlyTree,
//                           ProcessUtilityContext context,
//                           ParamListInfo params,
//                           QueryEnvironment *queryEnv,
//                           DestReceiver *dest,
//                           QueryCompletion *qc)
// {
//     Node *parsetree = pstmt->utilityStmt;

//     if (IsA(parsetree, AlterTableStmt))
//     {
//         AlterTableStmt *stmt = (AlterTableStmt *) parsetree;
//         RangeVar *rv = stmt->relation;

//         Oid relid = RangeVarGetRelid(rv, AccessExclusiveLock, true);
//         if (OidIsValid(relid))
//         {
//             Relation rel = table_open(relid, NoLock);

//             if (rel->rd_rel->relkind == RELKIND_BLOCKCHAIN_TABLE)
//             {
//                 ereport(ERROR,
//                         (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
//                          errmsg("ALTER not allowed on blockchain table \"%s\"", rv->relname),
//                          errdetail("This operation is not supported for blockchain tables.")));
//             }

//             table_close(rel, NoLock);
//         }
//     }
//     else if (IsA(parsetree, TruncateStmt))
//     {
//         TruncateStmt *stmt = (TruncateStmt *) parsetree;
//         ListCell *lc;

//         foreach(lc, stmt->relations)
//         {
//             RangeVar *rv = (RangeVar *) lfirst(lc);
//             Oid relid = RangeVarGetRelid(rv, AccessExclusiveLock, true);

//             if (OidIsValid(relid))
//             {
//                 Relation rel = table_open(relid, NoLock);

//                 if (rel->rd_rel->relkind == RELKIND_BLOCKCHAIN_TABLE)
//                 {
//                     ereport(ERROR,
//                             (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
//                              errmsg("TRUNCATE not allowed on blockchain table \"%s\"", rv->relname),
//                              errdetail("This operation is not supported for blockchain tables.")));
//                 }

//                 table_close(rel, NoLock);
//             }
//         }
//     }
//     else if (IsA(parsetree, DropStmt))
//     {
//         DropStmt *stmt = (DropStmt *) parsetree;
//         ListCell *lc;

//         foreach(lc, stmt->objects)
//         {
//             List *name = (List *) lfirst(lc);
//             RangeVar *rv = makeRangeVarFromNameList(name);

//             Oid relid = RangeVarGetRelid(rv, AccessExclusiveLock, true);
//             if (!OidIsValid(relid))
//                 continue;

//             Relation rel = table_open(relid, NoLock);

//             if (rel->rd_rel->relkind == RELKIND_BLOCKCHAIN_TABLE)
//             {
//                 // Allow for now
//                 ereport(WARNING,
//                         (errmsg("DROP on blockchain table \"%s\" is temporarily allowed", rv->relname)));
//             }

//             table_close(rel, NoLock);
//         }
//     }

//     /* Pass to next hook or standard */
//     if (prev_ProcessUtility_hook)
//         prev_ProcessUtility_hook(pstmt, queryString, readOnlyTree, context,
//                                  params, queryEnv, dest, qc);
//     else
//         standard_ProcessUtility(pstmt, queryString, readOnlyTree, context,
//                                 params, queryEnv, dest, qc);
// }


// void
// _PG_init(void)
// {
//     prev_ProcessUtility_hook = ProcessUtility_hook;
//     ProcessUtility_hook = blockchain_ProcessUtility;
// }
