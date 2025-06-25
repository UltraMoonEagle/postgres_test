/*

Blocking ALTER and TRUNCATE have been enforced manually in lower level tablecmds.c 
Extend this for AUDITING purposes

*/


#include "postgres.h"
#include "tcop/utility.h"
#include "catalog/pg_class.h"
#include "catalog/pg_am_d.h"
#include "catalog/namespace.h"
#include "utils/rel.h"
#include "utils/lsyscache.h"
#include "commands/defrem.h"
#include "access/htup_details.h"
#include "access/relation.h"
#include "access/table.h"
#include "access/xact.h"
#include "nodes/parsenodes.h"
#include "commands/tablecmds.h"
#include "commands/dbcommands.h"
#include "catalog/objectaccess.h"
#include "miscadmin.h"

static ProcessUtility_hook_type prev_ProcessUtility_hook = NULL;

static void
blockchain_ProcessUtility(PlannedStmt *pstmt,
                          const char *queryString,
                          bool readOnlyTree,
                          ProcessUtilityContext context,
                          ParamListInfo params,
                          QueryEnvironment *queryEnv,
                          DestReceiver *dest,
                          QueryCompletion *qc)
{
    Node *parsetree = pstmt->utilityStmt;
   

    /* Check for TRUNCATE, ALTER TABLE, DROP, VACUUM, etc. */
    if (IsA(parsetree, AlterTableStmt) ||
        IsA(parsetree, TruncateStmt) ||
        IsA(parsetree, DropStmt) ||
        IsA(parsetree, ClusterStmt) ||
        IsA(parsetree, ReindexStmt) ||
        IsA(parsetree, VacuumStmt))
    {
        Oid relid = InvalidOid;
        List *rels = NIL;

        /* Extract relation list depending on statement type */
        if (IsA(parsetree, AlterTableStmt))
            rels = list_make1(((AlterTableStmt *) parsetree)->relation);
        else if (IsA(parsetree, TruncateStmt))
            rels = ((TruncateStmt *) parsetree)->relations;
        else if (IsA(parsetree, DropStmt))
            rels = ((DropStmt *) parsetree)->objects;
        else if (IsA(parsetree, ClusterStmt))
            rels = list_make1(((ClusterStmt *) parsetree)->relation);
        else if (IsA(parsetree, ReindexStmt))
            rels = list_make1(((ReindexStmt *) parsetree)->relation);
        else if (IsA(parsetree, VacuumStmt))
            rels = ((VacuumStmt *) parsetree)->rels;

        ListCell *lc;

        foreach(lc, rels)
        {
            RangeVar *rv = NULL;
            if (IsA(lfirst(lc), RangeVar))
                rv = (RangeVar *) lfirst(lc);
            else if (IsA(lfirst(lc), ObjectWithArgs))  // DROP FUNCTION, etc.
                continue;

            if (!rv)
                continue;

            relid = RangeVarGetRelid(rv, AccessShareLock, true);
            if (!OidIsValid(relid))
                continue;

            Relation rel = try_relation_open(relid, AccessShareLock);
            if (!rel)
                continue;

            if (rel->rd_rel->relam == BLOCKCHAIN_TABLE_AM_OID)
            {
                relation_close(rel, AccessShareLock);
                ereport(ERROR,
                        (errmsg("operation not allowed on blockchain table \"%s\"",
                                rv->relname),
                         errdetail("DDL operation blocked by immutability enforcement.")));
            }

            relation_close(rel, AccessShareLock);
        }
    }

    /* Pass through to next hook or standard handler */
    if (prev_ProcessUtility_hook)
        prev_ProcessUtility_hook(pstmt, queryString, readOnlyTree,
                                 context, params, queryEnv, dest, qc);
    else
        standard_ProcessUtility(pstmt, queryString, readOnlyTree,
                                context, params, queryEnv, dest, qc);
}

void
blockchain_utility_hook_init(void)
{
    prev_ProcessUtility_hook = ProcessUtility_hook;
    ProcessUtility_hook = blockchain_ProcessUtility;
}

void
blockchain_utility_hook_fini(void)
{
    ProcessUtility_hook = prev_ProcessUtility_hook;
}
