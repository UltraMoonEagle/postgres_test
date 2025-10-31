-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION blockchain_anchor" to load this file. \quit

-- blockchain_anchor extension
-- Patent: "Tamper-Evident Anchoring of Arbitrary SQL Query Results"

-- Create anchor table function
CREATE FUNCTION create_anchor_table(table_name text)
RETURNS text
AS 'MODULE_PATHNAME', 'create_anchor_table'
LANGUAGE C STRICT;

COMMENT ON FUNCTION create_anchor_table(text) IS
'Create a blockchain table for storing query anchors with metadata';

-- Anchor query result function
CREATE FUNCTION anchor_query_result(
    anchor_table text,
    query_id text,
    query_text text,
    notes text DEFAULT NULL
)
RETURNS bytea
AS 'MODULE_PATHNAME', 'anchor_query_result'
LANGUAGE C;

COMMENT ON FUNCTION anchor_query_result(text, text, text, text) IS
'Anchor arbitrary SQL query result with deterministic hashing into blockchain table';

-- Verify query anchor function
CREATE FUNCTION verify_query_anchor(
    anchor_table text,
    query_id text
)
RETURNS boolean
AS 'MODULE_PATHNAME', 'verify_query_anchor'
LANGUAGE C STRICT;

COMMENT ON FUNCTION verify_query_anchor(text, text) IS
'Verify integrity of anchored query by re-executing and comparing hashes';
