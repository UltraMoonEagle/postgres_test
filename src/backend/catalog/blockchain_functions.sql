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
CREATE OR REPLACE FUNCTION is_blockchain_table_simple(table_name text)
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
                    (c.column_name LIKE '\\_\\_%')::boolean as is_system_column,  -- Escape underscores properly
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
                    (c.column_name LIKE '\\_\\_%')::int, -- System columns last
                    c.ordinal_position;
            END;
            $func$;
        $sql$;

        -- Function to get audit view of a blockchain table (showing all system columns)
        EXECUTE $sql$
            CREATE OR REPLACE FUNCTION blockchain_audit_view(table_name text)
            RETURNS TABLE (
                -- User columns will be dynamically added
                __row_id uuid,
                __curr_hash bytea,
                __prev_hash bytea,
                __tx_type text,
                __tx_lsn bigint,
                __tx_origin uuid,
                __tx_version integer,
                __is_latest boolean,
                __tx_timestamp timestamptz
            )
            LANGUAGE plpgsql
            AS $func$
            DECLARE
                query_text text;
                user_columns text;
                full_table_name text;
            BEGIN
                -- Handle schema qualification
                IF position('.' in table_name) > 0 THEN
                    full_table_name := table_name;
                ELSE
                    full_table_name := 'public.' || table_name;
                END IF;
                
                -- Build query to get user columns (non-system columns)
                SELECT string_agg(column_name, ', ' ORDER BY ordinal_position)
                INTO user_columns
                FROM information_schema.columns
                WHERE table_schema || '.' || information_schema.columns.table_name = full_table_name
                AND column_name NOT LIKE '\\_\\_%';
                
                -- Build the complete query
                IF user_columns IS NOT NULL THEN
                    query_text := format('SELECT %s, __row_id, __curr_hash, __prev_hash, __tx_type, __tx_lsn, __tx_origin, __tx_version, __is_latest, __tx_timestamp FROM %s',
                                       user_columns, full_table_name);
                ELSE
                    query_text := format('SELECT __row_id, __curr_hash, __prev_hash, __tx_type, __tx_lsn, __tx_origin, __tx_version, __is_latest, __tx_timestamp FROM %s',
                                       full_table_name);
                END IF;
                
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
                full_table_name text;
            BEGIN
                -- Handle schema qualification
                IF position('.' in table_name) > 0 THEN
                    full_table_name := table_name;
                ELSE
                    full_table_name := 'public.' || table_name;
                END IF;
                
                -- Build query to get user columns only (non-system columns)
                SELECT string_agg(column_name, ', ' ORDER BY ordinal_position)
                INTO user_columns
                FROM information_schema.columns
                WHERE table_schema || '.' || information_schema.columns.table_name = full_table_name
                AND column_name NOT LIKE '\\_\\_%';
                
                IF user_columns IS NULL THEN
                    RAISE EXCEPTION 'No user columns found in table %', table_name;
                END IF;
                
                -- Build and execute the query
                query_text := format('SELECT %s FROM %s', user_columns, full_table_name);
                
                RETURN QUERY EXECUTE query_text;
            END;
            $func$;
        $sql$;

        -- Function to get blockchain system columns only
        EXECUTE $sql$
            CREATE OR REPLACE FUNCTION blockchain_system_columns(table_name text)
            RETURNS TABLE (
                __row_id uuid,
                __curr_hash bytea,
                __prev_hash bytea,
                __tx_type text,
                __tx_lsn bigint,
                __tx_origin uuid,
                __tx_version integer,
                __is_latest boolean,
                __tx_timestamp timestamptz
            )
            LANGUAGE plpgsql
            AS $func$
            DECLARE
                query_text text;
                full_table_name text;
            BEGIN
                -- Handle schema qualification
                IF position('.' in table_name) > 0 THEN
                    full_table_name := table_name;
                ELSE
                    full_table_name := 'public.' || table_name;
                END IF;
                
                -- Build query to get system columns only
                query_text := format('SELECT __row_id, __curr_hash, __prev_hash, __tx_type, __tx_lsn, __tx_origin, __tx_version, __is_latest, __tx_timestamp FROM %s',
                                   full_table_name);
                
                RETURN QUERY EXECUTE query_text;
            END;
            $func$;
        $sql$;

    END IF;
END
$$;