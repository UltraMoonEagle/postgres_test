-- ============================================================================
-- PostgreSQL Blockchain Table - Comprehensive Test Suite
-- ============================================================================
-- This script tests all aspects of blockchain table functionality including:
-- - Table creation and basic operations
-- - System column hiding
-- - Counter persistence
-- - DDL/DML restrictions
-- - Column rename prevention
-- - Helper functions
-- ============================================================================

-- Clean up any existing test objects
DROP TABLE IF EXISTS blockchain_test CASCADE;
DROP TABLE IF EXISTS regular_test CASCADE;

-- ============================================================================
-- SECTION 1: Basic Blockchain Table Creation and Operations
-- ============================================================================
\echo '=== Section 1: Basic Blockchain Table Creation ==='

-- Create a blockchain table
CREATE BLOCKCHAIN TABLE blockchain_test (
    user_id INTEGER,
    username VARCHAR(50),
    email VARCHAR(100),
    action VARCHAR(200),
    ip_address INET,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Create a regular table for comparison
CREATE TABLE regular_test (
    user_id INTEGER,
    username VARCHAR(50)
);

-- ============================================================================
-- SECTION 2: Testing System Column Hiding
-- ============================================================================
\echo '=== Section 2: System Column Hiding Test ==='

-- Insert test data
INSERT INTO blockchain_test (user_id, username, email, action, ip_address) VALUES 
    (1, 'alice', 'alice@example.com', 'login', '192.168.1.100'),
    (2, 'bob', 'bob@example.com', 'create_account', '192.168.1.101'),
    (3, 'charlie', 'charlie@example.com', 'update_profile', '192.168.1.102');

\echo 'Test 2.1: SELECT * should hide system columns'
SELECT * FROM blockchain_test;

\echo 'Test 2.2: Explicit system column access should work'
SELECT user_id, username, __row_id, __tx_lsn, __curr_hash, __tx_timestamp 
FROM blockchain_test;

\echo 'Test 2.3: Show all system columns'
SELECT __row_id, __curr_hash, __prev_hash, __tx_type, __tx_lsn, 
       __tx_origin, __tx_version, __is_latest, __tx_timestamp
FROM blockchain_test
ORDER BY __tx_lsn;

-- ============================================================================
-- SECTION 3: Testing Helper Functions
-- ============================================================================
\echo '=== Section 3: Helper Functions Test ==='

\echo 'Test 3.1: Check if tables are blockchain tables'
SELECT 
    'blockchain_test' as table_name, 
    is_blockchain_table('blockchain_test') as is_blockchain
UNION ALL
SELECT 
    'regular_test' as table_name, 
    is_blockchain_table('regular_test') as is_blockchain;

\echo 'Test 3.2: Describe blockchain table structure'
SELECT * FROM describe_blockchain_table('blockchain_test');

-- ============================================================================
-- SECTION 4: Testing DDL/DML Restrictions
-- ============================================================================
\echo '=== Section 4: DDL/DML Restrictions Test ==='

\echo 'Test 4.1: INSERT should work'
INSERT INTO blockchain_test (user_id, username, email, action, ip_address) 
VALUES (4, 'david', 'david@example.com', 'logout', '192.168.1.103');

\echo 'Test 4.2: UPDATE should fail'
-- This should fail
UPDATE blockchain_test SET email = 'alice_new@example.com' WHERE user_id = 1;

\echo 'Test 4.3: DELETE should fail'
-- This should fail
DELETE FROM blockchain_test WHERE user_id = 4;

\echo 'Test 4.4: TRUNCATE should fail'
-- This should fail
TRUNCATE blockchain_test;

\echo 'Test 4.5: ALTER TABLE ADD COLUMN should fail'
-- This should fail
ALTER TABLE blockchain_test ADD COLUMN new_column VARCHAR(50);

\echo 'Test 4.6: ALTER TABLE DROP COLUMN should fail'
-- This should fail
ALTER TABLE blockchain_test DROP COLUMN action;

\echo 'Test 4.7: ALTER TABLE ALTER COLUMN TYPE should fail'
-- This should fail
ALTER TABLE blockchain_test ALTER COLUMN username TYPE TEXT;

\echo 'Test 4.8: CREATE INDEX should fail'
-- This should fail
CREATE INDEX idx_blockchain_test_username ON blockchain_test(username);

-- ============================================================================
-- SECTION 5: Testing Column Rename Restrictions
-- ============================================================================
\echo '=== Section 5: Column Rename Restrictions Test ==='

\echo 'Test 5.1: Renaming user column should work'
ALTER TABLE blockchain_test RENAME COLUMN username TO user_name;

-- Verify the rename
SELECT column_name, data_type, is_system_column 
FROM describe_blockchain_table('blockchain_test')
WHERE column_name IN ('username', 'user_name');

\echo 'Test 5.2: Renaming system column should fail'
-- This should fail
ALTER TABLE blockchain_test RENAME COLUMN __tx_lsn TO __sequence_number;

\echo 'Test 5.3: Another system column rename should fail'
-- This should fail
ALTER TABLE blockchain_test RENAME COLUMN __curr_hash TO __current_hash;

-- ============================================================================
-- SECTION 6: Testing Table Renaming (Should Work)
-- ============================================================================
\echo '=== Section 6: Table Renaming Test ==='

\echo 'Test 6.1: Table rename should work'
ALTER TABLE blockchain_test RENAME TO blockchain_audit_log;

-- Verify the rename
SELECT tablename FROM pg_tables 
WHERE tablename IN ('blockchain_test', 'blockchain_audit_log');

-- ============================================================================
-- SECTION 7: Testing Counter System and Persistence
-- ============================================================================
\echo '=== Section 7: Counter System Test ==='

\echo 'Test 7.1: Check counter sequence'
SELECT user_id, user_name, __tx_lsn 
FROM blockchain_audit_log 
ORDER BY __tx_lsn;

\echo 'Test 7.2: Insert more data to test counter increment'
INSERT INTO blockchain_audit_log (user_id, user_name, email, action, ip_address) VALUES 
    (5, 'eve', 'eve@example.com', 'delete_account', '192.168.1.104'),
    (6, 'frank', 'frank@example.com', 'password_reset', '192.168.1.105');

\echo 'Test 7.3: Verify counter continued from previous value'
SELECT user_id, user_name, __tx_lsn 
FROM blockchain_audit_log 
WHERE user_id IN (5, 6);

\echo 'Test 7.4: Failed operation should consume a counter'
-- This UPDATE will fail but should consume a counter
    UPDATE blockchain_audit_log SET email = 'test@example.com' WHERE user_id = 5;

-- Insert another record to see the gap in counter
INSERT INTO blockchain_audit_log (user_id, user_name, email, action, ip_address) 
VALUES (7, 'grace', 'grace@example.com', 'login', '192.168.1.106');

\echo 'Test 7.5: Check for counter gap after failed operation'
SELECT user_id, user_name, __tx_lsn 
FROM blockchain_audit_log 
WHERE user_id >= 5
ORDER BY __tx_lsn;

-- ============================================================================
-- SECTION 8: Testing Hash Chain Integrity
-- ============================================================================
\echo '=== Section 8: Hash Chain Integrity Test ==='

\echo 'Test 8.1: Verify hash chain linkage'
WITH hash_chain AS (
    SELECT 
        __tx_lsn,
        user_name,
        encode(__curr_hash, 'hex') as curr_hash_hex,
        encode(__prev_hash, 'hex') as prev_hash_hex,
        CASE 
            WHEN __tx_lsn = 1 THEN 'GENESIS'
            ELSE 'LINKED'
        END as chain_status
    FROM blockchain_audit_log
    ORDER BY __tx_lsn
)
SELECT 
    __tx_lsn as seq,
    user_name,
    substring(curr_hash_hex, 1, 16) || '...' as curr_hash_short,
    substring(prev_hash_hex, 1, 16) || '...' as prev_hash_short,
    chain_status
FROM hash_chain;

\echo 'Test 8.2: Verify first row has zero prev_hash'
SELECT 
    __tx_lsn,
    user_name,
    __prev_hash = '\x0000000000000000000000000000000000000000000000000000000000000000' as is_zero_hash
FROM blockchain_audit_log
WHERE __tx_lsn = 1;

-- ============================================================================
-- SECTION 9: Performance and Scalability Test
-- ============================================================================
\echo '=== Section 9: Performance Test ==='

\echo 'Test 9.1: Bulk insert test'
INSERT INTO blockchain_audit_log (user_id, user_name, email, action, ip_address)
SELECT 
    1000 + gs AS user_id,
    'user_' || gs AS user_name,
    'user_' || gs || '@example.com' AS email,
    CASE (gs % 4)
        WHEN 0 THEN 'login'
        WHEN 1 THEN 'logout'
        WHEN 2 THEN 'update_profile'
        WHEN 3 THEN 'delete_account'
    END AS action,
    ('192.168.1.' || (100 + (gs % 155)))::inet AS ip_address
FROM generate_series(1, 100) AS gs;

\echo 'Test 9.2: Verify bulk insert counters'
SELECT 
    MIN(__tx_lsn) as min_counter,
    MAX(__tx_lsn) as max_counter,
    COUNT(*) as total_rows,
    MAX(__tx_lsn) - MIN(__tx_lsn) + 1 as expected_range
FROM blockchain_audit_log
WHERE user_id >= 1000;

-- ============================================================================
-- SECTION 10: Summary Report
-- ============================================================================
\echo '=== Section 10: Summary Report ==='

\echo 'Total rows in blockchain table:'
SELECT COUNT(*) as total_rows FROM blockchain_audit_log;

\echo 'Counter range:'
SELECT 
    MIN(__tx_lsn) as first_counter,
    MAX(__tx_lsn) as last_counter,
    MAX(__tx_lsn) - MIN(__tx_lsn) + 1 as counters_used
FROM blockchain_audit_log;

\echo 'Table structure summary:'
SELECT 
    COUNT(*) FILTER (WHERE NOT is_system_column) as user_columns,
    COUNT(*) FILTER (WHERE is_system_column) as system_columns,
    COUNT(*) as total_columns
FROM describe_blockchain_table('blockchain_audit_log');

\echo 'Blockchain table verification:'
SELECT 
    'blockchain_audit_log' as table_name,
    is_blockchain_table('blockchain_audit_log') as is_blockchain,
    COUNT(*) as row_count,
    COUNT(DISTINCT __row_id) as unique_rows,
    MIN(__tx_timestamp) as first_insert,
    MAX(__tx_timestamp) as last_insert
FROM blockchain_audit_log;

-- ============================================================================
-- SECTION 11: Counter Persistence Test Instructions
-- ============================================================================
\echo '=== Section 11: Counter Persistence Test ==='
\echo 'To test counter persistence across server restarts:'
\echo '1. Note the current max counter value:'
SELECT MAX(__tx_lsn) as current_max_counter FROM blockchain_audit_log;

\echo '2. Restart PostgreSQL server:'
\echo '   pg_ctl restart -D $PGDATA'
\echo ''
\echo '3. After restart, run this query:'
\echo '   INSERT INTO blockchain_audit_log (user_id, user_name, email, action, ip_address)'
\echo '   VALUES (9999, ''restart_test'', ''test@example.com'', ''restart_test'', ''192.168.1.1'');'
\echo ''
\echo '4. Check that the counter continued from where it left off:'
\echo '   SELECT user_id, user_name, __tx_lsn FROM blockchain_audit_log WHERE user_id = 9999;'
\echo ''
\echo 'The __tx_lsn should be current_max_counter + 1, proving persistence works.'

-- ============================================================================
-- Cleanup (commented out - uncomment to clean up after tests)
-- ============================================================================
-- DROP TABLE blockchain_audit_log CASCADE;
-- DROP TABLE regular_test CASCADE;