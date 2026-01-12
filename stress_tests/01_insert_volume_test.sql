-- ============================================================================
-- INSERT VOLUME STRESS TEST
-- Tests system limits with increasing insert volumes
-- ============================================================================

\timing on
\echo '============================================================================'
\echo 'INSERT VOLUME STRESS TEST'
\echo '============================================================================'
\echo ''

-- Create test table
DROP BLOCKCHAIN TABLE IF EXISTS stress_test_inserts CASCADE;
CREATE BLOCKCHAIN TABLE stress_test_inserts (
    test_id INT,
    data TEXT,
    payload TEXT  -- Add some bulk to rows
);

\echo '=== Phase 1: 1,000 rows ==='
INSERT INTO stress_test_inserts
SELECT i, 'data_' || i, repeat('X', 100)
FROM generate_series(1, 1000) i;

SELECT COUNT(*) as total_rows FROM stress_test_inserts;
SELECT * FROM verify_hash_chain('stress_test_inserts');
\echo ''

\echo '=== Phase 2: 10,000 rows (total) ==='
INSERT INTO stress_test_inserts
SELECT i, 'data_' || i, repeat('X', 100)
FROM generate_series(1001, 10000) i;

SELECT COUNT(*) as total_rows FROM stress_test_inserts;
SELECT * FROM verify_hash_chain('stress_test_inserts');
\echo ''

\echo '=== Phase 3: 50,000 rows (total) ==='
INSERT INTO stress_test_inserts
SELECT i, 'data_' || i, repeat('X', 100)
FROM generate_series(10001, 50000) i;

SELECT COUNT(*) as total_rows FROM stress_test_inserts;
SELECT * FROM verify_hash_chain('stress_test_inserts');
\echo ''

\echo '=== Phase 4: 100,000 rows (total) ==='
INSERT INTO stress_test_inserts
SELECT i, 'data_' || i, repeat('X', 100)
FROM generate_series(50001, 100000) i;

SELECT COUNT(*) as total_rows FROM stress_test_inserts;
SELECT * FROM verify_hash_chain('stress_test_inserts');
\echo ''

-- Final statistics
\echo '=== FINAL STATISTICS ==='
SELECT
    COUNT(*) as total_rows,
    pg_size_pretty(pg_total_relation_size('stress_test_inserts')) as table_size
FROM stress_test_inserts;

SELECT * FROM verify_hash_chain('stress_test_inserts');

\echo ''
\echo 'Insert volume test complete!'
