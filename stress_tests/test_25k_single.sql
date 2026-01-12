-- Test 25k row single transaction
-- Finding the actual limit for MVP configuration

\timing on

DROP BLOCKCHAIN TABLE IF EXISTS stress_25k CASCADE;
CREATE BLOCKCHAIN TABLE stress_25k (
    test_id INT,
    data TEXT
);

\echo '=== Testing 25,000 rows in single transaction ==='
BEGIN;
INSERT INTO stress_25k (test_id, data)
SELECT i, 'data_' || i
FROM generate_series(1, 25000) i;
COMMIT;

\echo ''
\echo '=== Verification ==='
SELECT COUNT(*) as total_rows FROM stress_25k;
SELECT COUNT(*) as valid_hashes FROM stress_25k WHERE __curr_hash IS NOT NULL;

-- Manual hash chain check
SELECT
    COUNT(*) as total_rows,
    MIN(__tx_lsn) as first_counter,
    MAX(__tx_lsn) as last_counter,
    COUNT(DISTINCT __tx_lsn) as unique_counters
FROM stress_25k;
