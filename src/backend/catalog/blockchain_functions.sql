--
-- blockchain_functions.sql
--
-- SQL functions for accessing blockchain system columns
-- These functions expose the hidden blockchain system columns that are
-- normally not visible in SELECT * queries
--

-- Function to get audit view of a blockchain table (showing all system columns)
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
AS $$
DECLARE
    query_text text;
    user_columns text;
    full_table_name text;
BEGIN
    -- Get the fully qualified table name
    SELECT schemaname || '.' || tablename INTO full_table_name
    FROM pg_tables 
    WHERE tablename = table_name
    LIMIT 1;
    
    IF full_table_name IS NULL THEN
        RAISE EXCEPTION 'Table % not found', table_name;
    END IF;
    
    -- Build query to get user columns (non-system columns)
    SELECT string_agg(column_name, ', ' ORDER BY ordinal_position)
    INTO user_columns
    FROM information_schema.columns
    WHERE table_name = table_name
    AND column_name NOT LIKE '__%';
    
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
$$;

-- Function to get user view of a blockchain table (showing only user columns)
CREATE OR REPLACE FUNCTION blockchain_user_view(table_name text)
RETURNS SETOF record
LANGUAGE plpgsql
AS $$
DECLARE
    query_text text;
    user_columns text;
    full_table_name text;
BEGIN
    -- Get the fully qualified table name
    SELECT schemaname || '.' || tablename INTO full_table_name
    FROM pg_tables 
    WHERE tablename = table_name
    LIMIT 1;
    
    IF full_table_name IS NULL THEN
        RAISE EXCEPTION 'Table % not found', table_name;
    END IF;
    
    -- Build query to get user columns only (non-system columns)
    SELECT string_agg(column_name, ', ' ORDER BY ordinal_position)
    INTO user_columns
    FROM information_schema.columns
    WHERE table_name = table_name
    AND column_name NOT LIKE '__%';
    
    IF user_columns IS NULL THEN
        RAISE EXCEPTION 'No user columns found in table %', table_name;
    END IF;
    
    -- Build and execute the query
    query_text := format('SELECT %s FROM %s', user_columns, full_table_name);
    
    RETURN QUERY EXECUTE query_text;
END;
$$;

-- Function to get blockchain system columns only
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
AS $$
DECLARE
    query_text text;
    full_table_name text;
BEGIN
    -- Get the fully qualified table name
    SELECT schemaname || '.' || tablename INTO full_table_name
    FROM pg_tables 
    WHERE tablename = table_name
    LIMIT 1;
    
    IF full_table_name IS NULL THEN
        RAISE EXCEPTION 'Table % not found', table_name;
    END IF;
    
    -- Build query to get system columns only
    query_text := format('SELECT __row_id, __curr_hash, __prev_hash, __tx_type, __tx_lsn, __tx_origin, __tx_version, __is_latest, __tx_timestamp FROM %s',
                       full_table_name);
    
    RETURN QUERY EXECUTE query_text;
END;
$$;

-- Function to check if a table is a blockchain table
CREATE OR REPLACE FUNCTION is_blockchain_table(table_name text)
RETURNS boolean
LANGUAGE plpgsql
AS $$
DECLARE
    is_blockchain boolean := false;
    table_oid oid;
BEGIN
    -- Get the table OID
    SELECT oid INTO table_oid
    FROM pg_class
    WHERE relname = table_name
    AND relkind = 'r';
    
    IF table_oid IS NULL THEN
        RETURN false;
    END IF;
    
    -- Check if table uses blockchain access method
    SELECT EXISTS(
        SELECT 1 FROM pg_class c
        JOIN pg_am am ON c.relam = am.oid
        WHERE c.oid = table_oid
        AND am.amname = 'blockchain'
    ) INTO is_blockchain;
    
    RETURN is_blockchain;
END;
$$;

-- Convenience function to describe blockchain table structure
CREATE OR REPLACE FUNCTION describe_blockchain_table(table_name text)
RETURNS TABLE (
    column_name text,
    data_type text,
    is_system_column boolean,
    description text
)
LANGUAGE plpgsql
AS $$
BEGIN
    RETURN QUERY
    SELECT 
        c.column_name::text,
        c.data_type::text,
        (c.column_name LIKE '__%')::boolean as is_system_column,
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
    WHERE c.table_name = describe_blockchain_table.table_name
    ORDER BY 
        (c.column_name LIKE '__%')::int, -- System columns last
        c.ordinal_position;
END;
$$;