-- ================================================================
-- Install Query Anchor Functions
-- This script registers the new C functions using fmgr_internal_function
-- ================================================================

-- Drop existing functions if they exist
DROP FUNCTION IF EXISTS create_anchor_table(text) CASCADE;
DROP FUNCTION IF EXISTS anchor_query_result(text, text, text, text) CASCADE;
DROP FUNCTION IF EXISTS verify_query_anchor(text, text) CASCADE;

-- Register create_anchor_table function
CREATE OR REPLACE FUNCTION create_anchor_table(table_name text)
RETURNS text
AS 'fmgr_internal_function', 'create_anchor_table'
LANGUAGE internal STRICT;

COMMENT ON FUNCTION create_anchor_table(text) IS
'Create a blockchain table for storing query anchors with metadata';

-- Register anchor_query_result function
CREATE OR REPLACE FUNCTION anchor_query_result(
    anchor_table text,
    query_id text,
    query_text text,
    notes text DEFAULT NULL
)
RETURNS bytea
AS 'fmgr_internal_function', 'anchor_query_result'
LANGUAGE internal;

COMMENT ON FUNCTION anchor_query_result(text, text, text, text) IS
'Anchor arbitrary SQL query result with deterministic hashing into blockchain table';

-- Register verify_query_anchor function
CREATE OR REPLACE FUNCTION verify_query_anchor(
    anchor_table text,
    query_id text
)
RETURNS boolean
AS 'fmgr_internal_function', 'verify_query_anchor'
LANGUAGE internal STRICT;

COMMENT ON FUNCTION verify_query_anchor(text, text) IS
'Verify integrity of anchored query by re-executing and comparing hashes';

-- Show registered functions
\echo ''
\echo '✓ Query anchor functions installed successfully!'
\echo ''
\echo 'Available functions:'
SELECT
    proname as function_name,
    pg_get_function_arguments(oid) as arguments,
    pg_get_function_result(oid) as returns
FROM pg_proc
WHERE proname IN ('create_anchor_table', 'anchor_query_result', 'verify_query_anchor')
ORDER BY proname;
