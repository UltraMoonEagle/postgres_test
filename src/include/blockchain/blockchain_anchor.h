/*-------------------------------------------------------------------------
 *
 * blockchain_anchor.h
 *	  Query Result Anchoring for Blockchain Tables
 *
 * This module implements tamper-evident anchoring of arbitrary SQL query
 * results into blockchain tables, as described in the patent:
 * "Tamper-Evident Anchoring of Arbitrary SQL Query Results Using
 * Blockchain Tables in Relational Databases"
 *
 * Copyright (c) 2025, PostgreSQL Blockchain Extension
 *
 *-------------------------------------------------------------------------
 */
#ifndef BLOCKCHAIN_ANCHOR_H
#define BLOCKCHAIN_ANCHOR_H

#include "postgres.h"
#include "fmgr.h"

/* Function declarations for SQL-callable functions */
extern Datum anchor_query_result(PG_FUNCTION_ARGS);
extern Datum verify_query_anchor(PG_FUNCTION_ARGS);
extern Datum create_anchor_table(PG_FUNCTION_ARGS);

/* Legacy function (kept for compatibility) */
extern Datum anchor_bc_query(PG_FUNCTION_ARGS);

#endif							/* BLOCKCHAIN_ANCHOR_H */
