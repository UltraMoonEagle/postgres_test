/*-------------------------------------------------------------------------
 *
 * blockchain_anchor.c
 *	  Query Result Anchoring for Blockchain Tables
 *
 * This module implements the patent: "Tamper-Evident Anchoring of Arbitrary
 * SQL Query Results Using Blockchain Tables in Relational Databases"
 *
 * Key features:
 * - Anchor arbitrary SQL query results (including joins/aggregates)
 * - Deterministic serialization and sorting of result sets
 * - Cryptographic hashing (SHA-256) of query outputs
 * - Storage in blockchain tables with metadata
 * - Verification through query replay and hash comparison
 *
 * Copyright (c) 2025, PostgreSQL Blockchain Extension
 *
 *-------------------------------------------------------------------------
 */

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
#include "utils/array.h"
#include "catalog/pg_collation.h"

#include "blockchain/blockchain_hash.h"
#include "blockchain/blockchain_anchor.h"

PG_FUNCTION_INFO_V1(anchor_query_result);
PG_FUNCTION_INFO_V1(verify_query_anchor);
PG_FUNCTION_INFO_V1(create_anchor_table);
PG_FUNCTION_INFO_V1(anchor_bc_query); /* Legacy function */

/* Internal helper functions */
static bytea *hash_query_result_deterministic(const char *query_text, SPITupleTable *tuptable, uint64 nrows);
static int compare_tuples(const void *a, const void *b, void *arg);
static char *serialize_tuple(HeapTuple tuple, TupleDesc tupdesc);

/*
 * Comparison context for tuple sorting
 */
typedef struct TupleSortContext
{
	TupleDesc	tupdesc;
	int			nattrs;
} TupleSortContext;

/*
 * create_anchor_table
 *
 * Creates a blockchain table specifically for storing query anchors.
 * This table stores: query_id, query_text, result_hash, anchor_time,
 * user_id, and notes.
 *
 * Usage: SELECT create_anchor_table('my_anchor_table');
 */
Datum
create_anchor_table(PG_FUNCTION_ARGS)
{
	const char *table_name;
	StringInfoData sql;
	int			spi_ret;

	if (PG_ARGISNULL(0))
		elog(ERROR, "table name cannot be NULL");

	table_name = text_to_cstring(PG_GETARG_TEXT_PP(0));

	/* Validate table name to prevent SQL injection */
	if (strchr(table_name, '\'') != NULL || strchr(table_name, '"') != NULL)
		elog(ERROR, "invalid table name");

	initStringInfo(&sql);
	appendStringInfo(&sql,
					 "CREATE BLOCKCHAIN TABLE %s ("
					 "  query_id TEXT NOT NULL, "
					 "  query_text TEXT NOT NULL, "
					 "  result_hash BYTEA NOT NULL, "
					 "  anchor_time TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, "
					 "  user_id TEXT NOT NULL DEFAULT CURRENT_USER, "
					 "  notes TEXT"
					 ");",
					 table_name);

	spi_ret = SPI_connect();
	if (spi_ret != SPI_OK_CONNECT)
		elog(ERROR, "SPI_connect failed: %d", spi_ret);

	spi_ret = SPI_execute(sql.data, false, 0);
	if (spi_ret < 0)
		elog(ERROR, "Failed to create anchor table: %d", spi_ret);

	SPI_finish();
	pfree(sql.data);

	PG_RETURN_TEXT_P(cstring_to_text("Anchor table created successfully"));
}

/*
 * anchor_query_result
 *
 * Executes an arbitrary SQL query, deterministically serializes and sorts
 * the result set, computes a SHA-256 hash, and stores it in a blockchain
 * anchor table along with metadata.
 *
 * Arguments:
 *   - anchor_table: Name of the blockchain table to store anchors
 *   - query_id: Unique identifier for this query/anchor
 *   - query_text: The SQL query to execute and anchor
 *   - notes: Optional notes about this anchor
 *
 * Returns: The computed hash as bytea
 *
 * Example:
 *   SELECT anchor_query_result(
 *     'query_anchors',
 *     'customer_summary_fy25q2',
 *     'SELECT customer_id, SUM(amount) FROM transactions GROUP BY customer_id ORDER BY customer_id',
 *     'FY25 Q2 customer transaction summary'
 *   );
 */
Datum
anchor_query_result(PG_FUNCTION_ARGS)
{
	const char *anchor_table;
	const char *query_id;
	const char *query_text;
	const char *notes = NULL;
	bytea	   *result_hash = NULL;
	StringInfoData insert_sql;
	int			spi_ret;

	/* Validate arguments */
	if (PG_ARGISNULL(0))
		elog(ERROR, "anchor_table cannot be NULL");
	if (PG_ARGISNULL(1))
		elog(ERROR, "query_id cannot be NULL");
	if (PG_ARGISNULL(2))
		elog(ERROR, "query_text cannot be NULL");

	anchor_table = text_to_cstring(PG_GETARG_TEXT_PP(0));
	query_id = text_to_cstring(PG_GETARG_TEXT_PP(1));
	query_text = text_to_cstring(PG_GETARG_TEXT_PP(2));

	if (!PG_ARGISNULL(3))
		notes = text_to_cstring(PG_GETARG_TEXT_PP(3));

	/* Validate inputs to prevent SQL injection */
	if (strchr(anchor_table, '\'') != NULL || strchr(anchor_table, '"') != NULL)
		elog(ERROR, "invalid anchor table name");

	elog(INFO, "Anchoring query: %s", query_id);

	/* Step 1: Execute the query */
	spi_ret = SPI_connect();
	if (spi_ret != SPI_OK_CONNECT)
		elog(ERROR, "SPI_connect failed: %d", spi_ret);

	PG_TRY();
	{
		bytea	   *temp_hash;

		spi_ret = SPI_execute(query_text, true, 0);
		if (spi_ret != SPI_OK_SELECT)
		{
			SPI_finish();
			elog(ERROR, "Query execution failed: %d", spi_ret);
		}

		if (SPI_tuptable == NULL)
		{
			SPI_finish();
			elog(ERROR, "Query returned no tuple table");
		}

		/* Step 2: Compute deterministic hash of query result */
		temp_hash = hash_query_result_deterministic(query_text,
													SPI_tuptable,
													SPI_processed);

		/* Copy hash to upper context before SPI_finish destroys it */
		result_hash = (bytea *) SPI_palloc(VARSIZE(temp_hash));
		memcpy(result_hash, temp_hash, VARSIZE(temp_hash));

		SPI_finish();
	}
	PG_CATCH();
	{
		SPI_finish();
		PG_RE_THROW();
	}
	PG_END_TRY();

	/* Step 3: Insert hash and metadata into blockchain anchor table */
	initStringInfo(&insert_sql);
	appendStringInfo(&insert_sql,
					 "INSERT INTO %s (query_id, query_text, result_hash, notes) "
					 "VALUES ($1, $2, $3, $4)",
					 anchor_table);

	spi_ret = SPI_connect();
	if (spi_ret != SPI_OK_CONNECT)
		elog(ERROR, "SPI_connect failed for insert: %d", spi_ret);

	PG_TRY();
	{
		Oid			argtypes[4] = {TEXTOID, TEXTOID, BYTEAOID, TEXTOID};
		Datum		values[4];
		char		nulls[4];

		values[0] = CStringGetTextDatum(query_id);
		values[1] = CStringGetTextDatum(query_text);
		values[2] = PointerGetDatum(result_hash);
		values[3] = notes ? CStringGetTextDatum(notes) : (Datum) 0;

		nulls[0] = ' ';
		nulls[1] = ' ';
		nulls[2] = ' ';
		nulls[3] = notes ? ' ' : 'n';

		spi_ret = SPI_execute_with_args(insert_sql.data, 4, argtypes,
										 values, nulls, false, 0);
		if (spi_ret != SPI_OK_INSERT)
		{
			SPI_finish();
			elog(ERROR, "Failed to insert anchor: %d", spi_ret);
		}

		SPI_finish();
	}
	PG_CATCH();
	{
		SPI_finish();
		PG_RE_THROW();
	}
	PG_END_TRY();

	pfree(insert_sql.data);

	elog(INFO, "Query anchor stored successfully with hash: %s",
		 DatumGetCString(DirectFunctionCall1(byteaout,
											  PointerGetDatum(result_hash))));

	PG_RETURN_BYTEA_P(result_hash);
}

/*
 * verify_query_anchor
 *
 * Verifies the integrity of a previously anchored query by re-executing
 * the query, recomputing the hash, and comparing it with the stored hash.
 *
 * Arguments:
 *   - anchor_table: Name of the blockchain anchor table
 *   - query_id: The query identifier to verify
 *
 * Returns: TRUE if verification passes, FALSE otherwise
 *
 * Example:
 *   SELECT verify_query_anchor('query_anchors', 'customer_summary_fy25q2');
 */
Datum
verify_query_anchor(PG_FUNCTION_ARGS)
{
	const char *anchor_table;
	const char *query_id;
	char	   *stored_query_text = NULL;
	bytea	   *stored_hash = NULL;
	bytea	   *computed_hash = NULL;
	StringInfoData select_sql;
	int			spi_ret;
	bool		verified = false;

	/* Validate arguments */
	if (PG_ARGISNULL(0))
		elog(ERROR, "anchor_table cannot be NULL");
	if (PG_ARGISNULL(1))
		elog(ERROR, "query_id cannot be NULL");

	anchor_table = text_to_cstring(PG_GETARG_TEXT_PP(0));
	query_id = text_to_cstring(PG_GETARG_TEXT_PP(1));

	/* Validate inputs */
	if (strchr(anchor_table, '\'') != NULL || strchr(anchor_table, '"') != NULL)
		elog(ERROR, "invalid anchor table name");

	elog(INFO, "Verifying query anchor: %s", query_id);

	/* Step 1: Retrieve stored query_text and result_hash from anchor table */
	initStringInfo(&select_sql);
	appendStringInfo(&select_sql,
					 "SELECT query_text, result_hash FROM %s WHERE query_id = $1 ORDER BY anchor_time DESC LIMIT 1",
					 anchor_table);

	spi_ret = SPI_connect();
	if (spi_ret != SPI_OK_CONNECT)
		elog(ERROR, "SPI_connect failed: %d", spi_ret);

	PG_TRY();
	{
		Oid			argtypes[1] = {TEXTOID};
		Datum		values[1] = {CStringGetTextDatum(query_id)};
		char		nulls[1] = {' '};

		spi_ret = SPI_execute_with_args(select_sql.data, 1, argtypes,
										 values, nulls, true, 0);

		if (spi_ret != SPI_OK_SELECT)
		{
			SPI_finish();
			elog(ERROR, "Failed to retrieve anchor: %d", spi_ret);
		}

		if (SPI_processed == 0)
		{
			SPI_finish();
			elog(ERROR, "No anchor found for query_id: %s", query_id);
		}

		/* Extract query_text and result_hash */
		HeapTuple	tuple = SPI_tuptable->vals[0];
		TupleDesc	tupdesc = SPI_tuptable->tupdesc;
		char	   *temp_query_text;
		bytea	   *temp_hash;

		bool		isnull;
		Datum		query_text_datum = SPI_getbinval(tuple, tupdesc, 1, &isnull);
		if (isnull)
		{
			SPI_finish();
			elog(ERROR, "Stored query_text is NULL");
		}
		temp_query_text = TextDatumGetCString(query_text_datum);

		Datum		hash_datum = SPI_getbinval(tuple, tupdesc, 2, &isnull);
		if (isnull)
		{
			SPI_finish();
			elog(ERROR, "Stored result_hash is NULL");
		}
		temp_hash = DatumGetByteaP(hash_datum);

		/* Copy to upper context before SPI_finish destroys them */
		stored_query_text = SPI_palloc(strlen(temp_query_text) + 1);
		strcpy(stored_query_text, temp_query_text);

		stored_hash = (bytea *) SPI_palloc(VARSIZE(temp_hash));
		memcpy(stored_hash, temp_hash, VARSIZE(temp_hash));

		SPI_finish();
	}
	PG_CATCH();
	{
		SPI_finish();
		PG_RE_THROW();
	}
	PG_END_TRY();

	pfree(select_sql.data);

	/* Step 2: Re-execute the original query */
	spi_ret = SPI_connect();
	if (spi_ret != SPI_OK_CONNECT)
		elog(ERROR, "SPI_connect failed for query re-execution: %d", spi_ret);

	PG_TRY();
	{
		bytea	   *temp_computed_hash;

		spi_ret = SPI_execute(stored_query_text, true, 0);
		if (spi_ret != SPI_OK_SELECT)
		{
			SPI_finish();
			elog(ERROR, "Query re-execution failed: %d", spi_ret);
		}

		if (SPI_tuptable == NULL)
		{
			SPI_finish();
			elog(ERROR, "Query returned no tuple table");
		}

		/* Step 3: Recompute hash */
		temp_computed_hash = hash_query_result_deterministic(stored_query_text,
															 SPI_tuptable,
															 SPI_processed);

		/* Copy to upper context before SPI_finish destroys it */
		computed_hash = (bytea *) SPI_palloc(VARSIZE(temp_computed_hash));
		memcpy(computed_hash, temp_computed_hash, VARSIZE(temp_computed_hash));

		SPI_finish();
	}
	PG_CATCH();
	{
		SPI_finish();
		PG_RE_THROW();
	}
	PG_END_TRY();

	/* Step 4: Compare hashes */
	if (VARSIZE(stored_hash) == VARSIZE(computed_hash))
	{
		if (memcmp(VARDATA(stored_hash), VARDATA(computed_hash),
				   VARSIZE(stored_hash) - VARHDRSZ) == 0)
		{
			verified = true;
			elog(INFO, "✓ Verification PASSED: Query result matches anchored hash");
		}
		else
		{
			elog(WARNING, "✗ Verification FAILED: Hash mismatch detected");
			elog(WARNING, "This indicates tampering or data modification since anchor was created");
		}
	}
	else
	{
		elog(WARNING, "✗ Verification FAILED: Hash size mismatch");
	}

	PG_RETURN_BOOL(verified);
}

/*
 * hash_query_result_deterministic
 *
 * Computes a deterministic SHA-256 hash of a query result set.
 * The result set is sorted by all columns (lexicographically) to ensure
 * deterministic output regardless of query execution order.
 *
 * This is the core of the patent's novelty: deterministic serialization
 * and hashing of arbitrary SQL query results.
 */
static bytea *
hash_query_result_deterministic(const char *query_text, SPITupleTable *tuptable, uint64 nrows)
{
	bc_sha256_ctx ctx;
	bytea	   *result;
	TupleDesc	tupdesc = tuptable->tupdesc;
	HeapTuple  *sorted_tuples;
	TupleSortContext sort_ctx;

	bc_sha256_init(&ctx);

	/* First, hash the query text itself for additional integrity */
	bc_sha256_update(&ctx, (const uint8 *) query_text, strlen(query_text));

	if (nrows == 0)
	{
		/* Empty result set */
		uint8		digest[BC_SHA256_DIGEST_LENGTH];
		bc_sha256_final(&ctx, digest);

		result = (bytea *) palloc(VARHDRSZ + BC_SHA256_DIGEST_LENGTH);
		SET_VARSIZE(result, VARHDRSZ + BC_SHA256_DIGEST_LENGTH);
		memcpy(VARDATA(result), digest, BC_SHA256_DIGEST_LENGTH);
		return result;
	}

	/* Step 1: Sort tuples deterministically */
	sorted_tuples = (HeapTuple *) palloc(sizeof(HeapTuple) * nrows);
	for (uint64 i = 0; i < nrows; i++)
		sorted_tuples[i] = tuptable->vals[i];

	sort_ctx.tupdesc = tupdesc;
	sort_ctx.nattrs = tupdesc->natts;

	/* Sort using qsort_arg to pass context */
	qsort_arg(sorted_tuples, nrows, sizeof(HeapTuple),
			  compare_tuples, &sort_ctx);

	/* Step 2: Serialize and hash each tuple in sorted order */
	for (uint64 i = 0; i < nrows; i++)
	{
		char	   *serialized = serialize_tuple(sorted_tuples[i], tupdesc);
		bc_sha256_update(&ctx, (const uint8 *) serialized, strlen(serialized));
		pfree(serialized);
	}

	pfree(sorted_tuples);

	/* Step 3: Finalize hash */
	uint8		digest[BC_SHA256_DIGEST_LENGTH];
	bc_sha256_final(&ctx, digest);

	result = (bytea *) palloc(VARHDRSZ + BC_SHA256_DIGEST_LENGTH);
	SET_VARSIZE(result, VARHDRSZ + BC_SHA256_DIGEST_LENGTH);
	memcpy(VARDATA(result), digest, BC_SHA256_DIGEST_LENGTH);

	return result;
}

/*
 * compare_tuples
 *
 * Comparison function for sorting tuples deterministically.
 * Compares tuples column by column, left to right.
 */
static int
compare_tuples(const void *a, const void *b, void *arg)
{
	HeapTuple	tuple_a = *(HeapTuple *) a;
	HeapTuple	tuple_b = *(HeapTuple *) b;
	TupleSortContext *ctx = (TupleSortContext *) arg;
	TupleDesc	tupdesc = ctx->tupdesc;

	for (int i = 0; i < ctx->nattrs; i++)
	{
		Form_pg_attribute attr = TupleDescAttr(tupdesc, i);
		bool		isnull_a,
					isnull_b;
		Datum		val_a,
					val_b;
		int			cmp;

		if (attr->attisdropped)
			continue;

		val_a = heap_getattr(tuple_a, i + 1, tupdesc, &isnull_a);
		val_b = heap_getattr(tuple_b, i + 1, tupdesc, &isnull_b);

		/* NULL handling: NULL sorts last */
		if (isnull_a && isnull_b)
			continue;
		if (isnull_a)
			return 1;
		if (isnull_b)
			return -1;

		/* Compare values using type-specific comparison */
		Oid			typoutput;
		bool		typisvarlena;
		char	   *str_a,
				   *str_b;

		getTypeOutputInfo(attr->atttypid, &typoutput, &typisvarlena);
		str_a = OidOutputFunctionCall(typoutput, val_a);
		str_b = OidOutputFunctionCall(typoutput, val_b);

		cmp = strcmp(str_a, str_b);

		pfree(str_a);
		pfree(str_b);

		if (cmp != 0)
			return cmp;
	}

	return 0;
}

/*
 * serialize_tuple
 *
 * Serializes a single tuple to a string representation for hashing.
 * Format: "col1_value|col2_value|col3_value|..."
 */
static char *
serialize_tuple(HeapTuple tuple, TupleDesc tupdesc)
{
	StringInfoData buf;
	initStringInfo(&buf);

	for (int i = 0; i < tupdesc->natts; i++)
	{
		Form_pg_attribute attr = TupleDescAttr(tupdesc, i);
		bool		isnull;
		Datum		val;

		if (attr->attisdropped)
			continue;

		val = heap_getattr(tuple, i + 1, tupdesc, &isnull);

		if (i > 0)
			appendStringInfoChar(&buf, '|');

		if (isnull)
		{
			appendStringInfoString(&buf, "<NULL>");
		}
		else
		{
			Oid			typoutput;
			bool		typisvarlena;
			char	   *str;

			getTypeOutputInfo(attr->atttypid, &typoutput, &typisvarlena);
			str = OidOutputFunctionCall(typoutput, val);
			appendStringInfoString(&buf, str);
			pfree(str);
		}
	}

	return buf.data;
}

/*
 * anchor_bc_query (Legacy function)
 *
 * Original simple implementation, kept for backward compatibility.
 * Does not include deterministic sorting or full metadata.
 */
Datum
anchor_bc_query(PG_FUNCTION_ARGS)
{
	const char *target_table = text_to_cstring(PG_GETARG_TEXT_PP(0));
	const char *query = text_to_cstring(PG_GETARG_TEXT_PP(1));
	bytea	   *result_hash = NULL;
	StringInfoData sql;
	int			spi_ret;

	initStringInfo(&sql);

	PG_TRY();
	{
		spi_ret = SPI_connect();
		if (spi_ret != SPI_OK_CONNECT)
			elog(ERROR, "SPI_connect failed: %d", spi_ret);

		spi_ret = SPI_execute(query, true, 0);
		if (spi_ret != SPI_OK_SELECT)
		{
			SPI_finish();
			elog(ERROR, "SPI_execute failed: %d", spi_ret);
		}

		if (SPI_tuptable == NULL || SPI_processed == 0)
		{
			SPI_finish();
			elog(ERROR, "Query returned no rows");
		}

		/* Use deterministic hashing */
		result_hash = hash_query_result_deterministic(query, SPI_tuptable, SPI_processed);

		SPI_finish();
	}
	PG_CATCH();
	{
		SPI_finish();
		PG_RE_THROW();
	}
	PG_END_TRY();

	appendStringInfo(&sql,
					 "INSERT INTO %s(anchor_hash) VALUES ($1);", target_table);

	PG_TRY();
	{
		Oid			argtypes[1] = {BYTEAOID};
		Datum		values[1] = {PointerGetDatum(result_hash)};
		char		nulls[1] = {' '};

		spi_ret = SPI_connect();
		if (spi_ret != SPI_OK_CONNECT)
			elog(ERROR, "SPI_connect (for insert) failed: %d", spi_ret);

		spi_ret = SPI_execute_with_args(sql.data, 1, argtypes, values, nulls, false, 0);
		if (spi_ret != SPI_OK_INSERT)
		{
			SPI_finish();
			elog(ERROR, "SPI insert failed: %d", spi_ret);
		}

		SPI_finish();
	}
	PG_CATCH();
	{
		SPI_finish();
		PG_RE_THROW();
	}
	PG_END_TRY();

	pfree(sql.data);

	PG_RETURN_VOID();
}
