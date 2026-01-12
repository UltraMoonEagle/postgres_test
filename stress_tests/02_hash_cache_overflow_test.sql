-- ============================================================================
-- HASH CACHE OVERFLOW TEST
-- Tests what happens when uncommitted transactions exceed 10k cache limit
-- ============================================================================

\timing on
\echo '============================================================================'
\echo 'HASH CACHE OVERFLOW TEST - Testing >10k uncommitted transactions'
\echo '============================================================================'
\echo ''

-- Create test table
DROP BLOCKCHAIN TABLE IF EXISTS stress_test_cache CASCADE;
CREATE BLOCKCHAIN TABLE stress_test_cache (
    test_id INT,
    data TEXT
);

\echo '=== Phase 1: Insert 5,000 rows in single transaction (below cache limit) ==='
BEGIN;
INSERT INTO stress_test_cache
SELECT i, 'data_' || i
FROM generate_series(1, 5000) i;
COMMIT;

SELECT COUNT(*) as total_rows FROM stress_test_cache;
SELECT * FROM verify_hash_chain('stress_test_cache');
\echo ''

\echo '=== Phase 2: Insert 15,000 rows in SINGLE transaction (EXCEEDS 10k cache!) ==='
\echo 'This will test hash cache overflow behavior...'
BEGIN;
INSERT INTO stress_test_cache
SELECT i, 'data_' || i
FROM generate_series(5001, 20000) i;
\echo 'Inserted 15,000 rows in one transaction - cache should overflow!'
COMMIT;

SELECT COUNT(*) as total_rows FROM stress_test_cache;
SELECT * FROM verify_hash_chain('stress_test_cache');
\echo ''

\echo '=== Phase 3: Insert 25,000 MORE rows in SINGLE transaction ==='
BEGIN;
INSERT INTO stress_test_cache
SELECT i, 'data_' || i
FROM generate_series(20001, 45000) i;
\echo 'Inserted 25,000 rows in one transaction!'
COMMIT;

SELECT COUNT(*) as total_rows FROM stress_test_cache;
SELECT * FROM verify_hash_chain('stress_test_cache');
\echo ''

-- Test if hash chain is still valid
\echo '=== VERIFICATION: Check if hash chain survived cache overflow ==='
SELECT * FROM verify_hash_chain('stress_test_cache');

-- Export some sample hashes to verify manually
\echo ''
\echo '=== Sample hash chain (first 10 and last 10 entries) ==='
(SELECT * FROM export_hash_chain('stress_test_cache') ORDER BY seq LIMIT 10)
UNION ALL
(SELECT * FROM export_hash_chain('stress_test_cache') ORDER BY seq DESC LIMIT 10)
ORDER BY seq;

\echo ''
\echo 'Hash cache overflow test complete!'
\echo 'If broken_links > 0, the cache overflow caused issues!'
