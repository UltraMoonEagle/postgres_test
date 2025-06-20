// File: src/include/commands/blockchain_views.h

#ifndef BLOCKCHAIN_VIEWS_H
#define BLOCKCHAIN_VIEWS_H

#include "nodes/parsenodes.h"  // for CreateStmt
#include "nodes/primnodes.h"   // for RangeVar
#include "postgres.h"

/*
 * Called from DefineRelation() to auto-create:
 * - user_view_<table>
 * - audit_view_<table>
 * - verify_chain_<table>(uuid)
 */
extern void MaybeCreateBlockchainViews(CreateStmt *stmt, Oid relid);

#endif /* BLOCKCHAIN_VIEWS_H */
