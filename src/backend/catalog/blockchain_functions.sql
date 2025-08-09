--
-- blockchain_functions.sql
--
-- SQL functions for accessing blockchain system columns
-- These functions expose the hidden blockchain system columns that are
-- normally not visible in SELECT * queries
--
-- This file is designed to work during PostgreSQL bootstrap (initdb)
-- by providing basic SQL-only functions first, then conditionally loading
-- advanced PL/pgSQL functions only after the language is available.
--

-- ============================================================================
-- BOOTSTRAP-SAFE FUNCTIONS (SQL-only, no PL/pgSQL dependency)
-- ============================================================================

-- Simple SQL-only version of is_blockchain_table for bootstrap
CREATE OR REPLACE FUNCTION is_blockchain_table(table_name text)
RETURNS boolean
LANGUAGE SQL
AS $$
    SELECT EXISTS(
        SELECT 1 FROM pg_class c
        JOIN pg_am am ON c.relam = am.oid
        JOIN pg_namespace n ON c.relnamespace = n.oid
        WHERE c.relname = $1
        AND n.nspname = 'public'
        AND c.relkind = 'b'  -- 'b' for blockchain tables
        AND am.amname = 'blockchain'
    );
$$;

-- Simple function to list blockchain tables
CREATE OR REPLACE FUNCTION list_blockchain_tables()
RETURNS TABLE(schema_name name, table_name name)
LANGUAGE SQL
AS $$
    SELECT n.nspname, c.relname
    FROM pg_class c
    JOIN pg_am am ON c.relam = am.oid
    JOIN pg_namespace n ON c.relnamespace = n.oid
    WHERE c.relkind = 'b'
    AND am.amname = 'blockchain'
    ORDER BY n.nspname, c.relname;
$$;

-- ============================================================================
-- CONDITIONAL PL/PGSQL FUNCTIONS (Load only if language is available)
-- ============================================================================

-- Check if plpgsql is available and load advanced functions
DO $$
BEGIN
    -- Only create advanced PL/pgSQL functions if the language is available
    IF EXISTS (SELECT 1 FROM pg_language WHERE lanname = 'plpgsql') THEN
        
        -- Function to check if a table is a blockchain table (ADVANCED VERSION)
        EXECUTE $sql$
            CREATE OR REPLACE FUNCTION is_blockchain_table(table_name text)
            RETURNS boolean
            LANGUAGE plpgsql
            AS $func$
            DECLARE
                is_blockchain boolean := false;
                schema_name text := 'public';
                relation_name text;
            BEGIN
                -- Parse schema and table name
                IF position('.' in table_name) > 0 THEN
                    schema_name := split_part(table_name, '.', 1);
                    relation_name := split_part(table_name, '.', 2);
                ELSE
                    relation_name := table_name;
                END IF;
                
                -- Check if table uses blockchain access method
                -- Note: blockchain tables have relkind = 'b', not 'r'
                SELECT EXISTS(
                    SELECT 1 FROM pg_class c
                    JOIN pg_am am ON c.relam = am.oid
                    JOIN pg_namespace n ON c.relnamespace = n.oid
                    WHERE c.relname = relation_name
                    AND n.nspname = schema_name
                    AND c.relkind = 'b'  -- 'b' for blockchain tables
                    AND am.amname = 'blockchain'
                ) INTO is_blockchain;
                
                RETURN is_blockchain;
            END;
            $func$;
        $sql$;

        -- Function to describe blockchain table structure
        EXECUTE $sql$
            CREATE OR REPLACE FUNCTION describe_blockchain_table(table_name text)
            RETURNS TABLE (
                column_name text,
                data_type text,
                is_system_column boolean,
                description text
            )
            LANGUAGE plpgsql
            AS $func$
            DECLARE
                schema_name text := 'public';
                relation_name text;
            BEGIN
                -- Parse schema and table name
                IF position('.' in table_name) > 0 THEN
                    schema_name := split_part(table_name, '.', 1);
                    relation_name := split_part(table_name, '.', 2);
                ELSE
                    relation_name := table_name;
                END IF;
                
                RETURN QUERY
                SELECT 
                    c.column_name::text,
                    c.data_type::text,
                    (c.column_name ~ '^__.*')::boolean as is_system_column,  -- Fixed regex for __ prefix
                    CASE 
                        WHEN c.column_name = '__row_id' THEN 'Unique identifier for this row'
                        WHEN c.column_name = '__curr_hash' THEN 'Hash of current row (SHA256)'
                        WHEN c.column_name = '__prev_hash' THEN 'Hash of previous row in chain'
                        WHEN c.column_name = '__tx_type' THEN 'Transaction type (INSERT/UPDATE/DELETE)'
                        WHEN c.column_name = '__tx_lsn' THEN 'Transaction sequence number (counter)'
                        WHEN c.column_name = '__tx_origin' THEN 'Transaction origin identifier'
                        WHEN c.column_name = '__tx_version' THEN 'Row version number'
                        WHEN c.column_name = '__is_latest' THEN 'Whether this is the latest version'
                        WHEN c.column_name = '__tx_timestamp' THEN 'Transaction timestamp'
                        ELSE 'User data column'
                    END::text as description
                FROM information_schema.columns c
                WHERE c.table_schema = schema_name
                AND c.table_name = relation_name
                ORDER BY 
                    (c.column_name ~ '^__.*')::int, -- System columns last (0 = user, 1 = system)
                    c.ordinal_position;
            END;
            $func$;
        $sql$;

        -- Function to get audit view of a blockchain table (showing all system columns)
        EXECUTE $sql$
            CREATE OR REPLACE FUNCTION blockchain_audit_view(table_name text)
            RETURNS SETOF record
            LANGUAGE plpgsql
            AS $func$
            DECLARE
                query_text text;
                all_columns text;
                schema_name text := 'public';
                relation_name text;
            BEGIN
                -- Parse schema and table name
                IF position('.' in table_name) > 0 THEN
                    schema_name := split_part(table_name, '.', 1);
                    relation_name := split_part(table_name, '.', 2);
                ELSE
                    relation_name := table_name;
                END IF;
                
                -- Build query to get ALL columns explicitly (both user and system)
                SELECT string_agg(column_name, ', ' ORDER BY ordinal_position)
                INTO all_columns
                FROM information_schema.columns
                WHERE table_schema = schema_name
                AND information_schema.columns.table_name = relation_name;
                
                IF all_columns IS NULL THEN
                    RAISE EXCEPTION 'No columns found in table %', table_name;
                END IF;
                
                -- Build and execute the query with explicit column list
                query_text := format('SELECT %s FROM %s.%s', all_columns, schema_name, relation_name);
                
                RETURN QUERY EXECUTE query_text;
            END;
            $func$;
        $sql$;

        -- Function to get user view of a blockchain table (showing only user columns)
        EXECUTE $sql$
            CREATE OR REPLACE FUNCTION blockchain_user_view(table_name text)
            RETURNS SETOF record
            LANGUAGE plpgsql
            AS $func$
            DECLARE
                query_text text;
                user_columns text;
                schema_name text := 'public';
                relation_name text;
            BEGIN
                -- Parse schema and table name
                IF position('.' in table_name) > 0 THEN
                    schema_name := split_part(table_name, '.', 1);
                    relation_name := split_part(table_name, '.', 2);
                ELSE
                    relation_name := table_name;
                END IF;
                
                -- Build query to get user columns only (non-system columns)
                SELECT string_agg(column_name, ', ' ORDER BY ordinal_position)
                INTO user_columns
                FROM information_schema.columns
                WHERE table_schema = schema_name
                AND information_schema.columns.table_name = relation_name
                AND NOT (column_name ~ '^__');
                
                IF user_columns IS NULL THEN
                    RAISE EXCEPTION 'No user columns found in table %', table_name;
                END IF;
                
                -- Build and execute the query
                query_text := format('SELECT %s FROM %s.%s', user_columns, schema_name, relation_name);
                
                RETURN QUERY EXECUTE query_text;
            END;
            $func$;
        $sql$;

        -- Function to get blockchain system columns only
        EXECUTE $sql$
            CREATE OR REPLACE FUNCTION blockchain_system_columns(table_name text)
            RETURNS SETOF record
            LANGUAGE plpgsql
            AS $func$
            DECLARE
                query_text text;
                schema_name text := 'public';
                relation_name text;
            BEGIN
                -- Parse schema and table name
                IF position('.' in table_name) > 0 THEN
                    schema_name := split_part(table_name, '.', 1);
                    relation_name := split_part(table_name, '.', 2);
                ELSE
                    relation_name := table_name;
                END IF;
                
                -- Build query to get system columns only
                query_text := format('SELECT __row_id, __curr_hash, __prev_hash, __tx_type, __tx_lsn, __tx_origin, __tx_version, __is_latest, __tx_timestamp FROM %s.%s', schema_name, relation_name);
                
                RETURN QUERY EXECUTE query_text;
            END;
            $func$;
        $sql$;

        -- Helper function to get blockchain table column information
        EXECUTE $sql$
            CREATE OR REPLACE FUNCTION blockchain_table_columns(table_name text)
            RETURNS TABLE(
                column_name text,
                data_type text,
                is_system boolean,
                ordinal_position integer
            )
            LANGUAGE plpgsql
            AS $func$
            DECLARE
                schema_name text := 'public';
                relation_name text;
            BEGIN
                -- Parse schema and table name
                IF position('.' in table_name) > 0 THEN
                    schema_name := split_part(table_name, '.', 1);
                    relation_name := split_part(table_name, '.', 2);
                ELSE
                    relation_name := table_name;
                END IF;
                
                RETURN QUERY
                SELECT 
                    c.column_name::text,
                    c.data_type::text,
                    (c.column_name ~ '^__.*')::boolean as is_system,
                    c.ordinal_position::integer
                FROM information_schema.columns c
                WHERE c.table_schema = schema_name
                AND c.table_name = relation_name
                ORDER BY c.ordinal_position;
            END;
            $func$;
        $sql$;

        -- Function to show user-friendly blockchain table info
        EXECUTE $sql$
            CREATE OR REPLACE FUNCTION blockchain_table_info(table_name text)
            RETURNS TABLE(
                user_columns integer,
                system_columns integer,
                total_columns integer,
                table_size text
            )
            LANGUAGE plpgsql
            AS $func$
            DECLARE
                full_table_name text;
                user_count integer;
                system_count integer;
                size_text text;
            BEGIN
                -- Handle schema qualification
                IF position('.' in table_name) > 0 THEN
                    full_table_name := table_name;
                ELSE
                    full_table_name := 'public.' || table_name;
                END IF;
                
                -- Count user columns
                SELECT count(*)::integer INTO user_count
                FROM information_schema.columns c
                WHERE c.table_schema || '.' || c.table_name = full_table_name
                AND c.column_name !~ '^__.*';
                
                -- Count system columns
                SELECT count(*)::integer INTO system_count
                FROM information_schema.columns c
                WHERE c.table_schema || '.' || c.table_name = full_table_name
                AND c.column_name ~ '^__.*';
                
                -- Get table size
                SELECT pg_size_pretty(pg_total_relation_size(full_table_name::regclass)) INTO size_text;
                
                RETURN QUERY
                SELECT user_count, system_count, user_count + system_count, size_text;
            END;
            $func$;
        $sql$;

        -- Function to verify hash chain integrity
        EXECUTE $sql$
            CREATE OR REPLACE FUNCTION verify_hash_chain(table_name text)
            RETURNS TABLE(
                total_rows bigint,
                broken_links bigint, 
                chain_integrity text,
                details text
            )
            LANGUAGE plpgsql
            AS $func$
            DECLARE
                schema_name text := 'public';
                relation_name text;
                query_text text;
                row_count bigint;
                broken_count bigint;
            BEGIN
                -- Parse schema and table name
                IF position('.' in table_name) > 0 THEN
                    schema_name := split_part(table_name, '.', 1);
                    relation_name := split_part(table_name, '.', 2);
                ELSE
                    relation_name := table_name;
                END IF;
                
                -- First check if table is a blockchain table
                IF NOT is_blockchain_table(table_name) THEN
                    RAISE EXCEPTION 'Table % is not a blockchain table', table_name;
                END IF;
                
                -- Get total row count
                query_text := format('SELECT COUNT(*) FROM %s.%s', schema_name, relation_name);
                EXECUTE query_text INTO row_count;
                
                -- Check hash chain integrity using the same logic as run_concurrent_tests.sh
                query_text := format($q$
                    WITH hash_check AS (
                        SELECT 
                            __tx_lsn,
                            __prev_hash,
                            LAG(__curr_hash) OVER (ORDER BY __tx_lsn) as expected_prev_hash
                        FROM %s.%s
                        ORDER BY __tx_lsn
                    )
                    SELECT COUNT(*) FILTER (WHERE __tx_lsn > 1 AND __prev_hash != expected_prev_hash)
                    FROM hash_check
                $q$, schema_name, relation_name);
                
                EXECUTE query_text INTO broken_count;
                
                RETURN QUERY
                SELECT 
                    row_count,
                    broken_count,
                    CASE 
                        WHEN broken_count = 0 THEN 'PASS: Hash chain intact'
                        ELSE 'FAIL: Broken hash chain detected'
                    END::text as chain_integrity,
                    CASE 
                        WHEN row_count = 0 THEN 'Empty table - no hash chain to verify'
                        WHEN row_count = 1 THEN 'Single row - hash chain trivially valid'
                        WHEN broken_count = 0 THEN format('All %s hash links are valid', row_count - 1)
                        ELSE format('%s out of %s hash links are broken', broken_count, row_count - 1)
                    END::text as details;
            END;
            $func$;
        $sql$;

        -- Function to export hash chain details (similar to run_concurrent_tests.sh CSV export)
        EXECUTE $sql$
            CREATE OR REPLACE FUNCTION export_hash_chain(table_name text)
            RETURNS TABLE(
                seq bigint,
                curr_hash_hex text,
                prev_hash_hex text, 
                chain_status text,
                is_valid boolean
            )
            LANGUAGE plpgsql  
            AS $func$
            DECLARE
                schema_name text := 'public';
                relation_name text;
                query_text text;
            BEGIN
                -- Parse schema and table name
                IF position('.' in table_name) > 0 THEN
                    schema_name := split_part(table_name, '.', 1);
                    relation_name := split_part(table_name, '.', 2);
                ELSE
                    relation_name := table_name;
                END IF;
                
                -- Check if table is a blockchain table
                IF NOT is_blockchain_table(table_name) THEN
                    RAISE EXCEPTION 'Table % is not a blockchain table', table_name;
                END IF;
                
                -- Export hash chain with validation (based on run_concurrent_tests.sh logic)
                query_text := format($q$
                    WITH hash_chain AS (
                        SELECT 
                            __tx_lsn,
                            encode(__curr_hash, 'hex') as curr_hash_hex,
                            encode(__prev_hash, 'hex') as prev_hash_hex,
                            LAG(encode(__curr_hash, 'hex')) OVER (ORDER BY __tx_lsn) as expected_prev_hex,
                            CASE 
                                WHEN __tx_lsn = 1 THEN 'GENESIS'
                                ELSE 'LINKED'
                            END as chain_status
                        FROM %s.%s
                        ORDER BY __tx_lsn
                    )
                    SELECT 
                        __tx_lsn as seq,
                        curr_hash_hex,
                        prev_hash_hex,
                        chain_status,
                        CASE 
                            WHEN __tx_lsn = 1 THEN true  -- Genesis is always valid
                            WHEN prev_hash_hex = expected_prev_hex THEN true
                            ELSE false
                        END as is_valid
                    FROM hash_chain
                    ORDER BY __tx_lsn
                $q$, schema_name, relation_name);
                
                RETURN QUERY EXECUTE query_text;
            END;
            $func$;
        $sql$;

    END IF;
END
$$;