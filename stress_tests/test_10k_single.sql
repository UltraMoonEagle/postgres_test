-- Test 10k row single transaction
\timing on

DROP BLOCKCHAIN TABLE IF EXISTS stress_10k CASCADE;
CREATE BLOCKCHAIN TABLE stress_10k (
    test_id INT,
    data TEXT
);

\echo '=== Testing 10,000 rows in single transaction ==='
BEGIN;
INSERT INTO stress_10k (test_id, data)
SELECT i, 'data_' || i
FROM generate_series(1, 10000) i;
COMMIT;

SELECT COUNT(*) as total_rows FROM stress_10k;
SELECT * FROM verify_hash_chain('stress_10k');
