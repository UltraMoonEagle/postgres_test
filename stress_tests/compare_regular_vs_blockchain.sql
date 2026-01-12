-- ============================================================================
-- Performance Comparison: Regular Table vs Blockchain Table
-- ============================================================================

\timing on

-- Create regular table
DROP TABLE IF EXISTS regular_table CASCADE;
CREATE TABLE regular_table (
    id SERIAL PRIMARY KEY,
    test_id INT,
    data TEXT
);

-- Create blockchain table
DROP BLOCKCHAIN TABLE IF EXISTS blockchain_table CASCADE;
CREATE BLOCKCHAIN TABLE blockchain_table (
    test_id INT,
    data TEXT
);

\echo ''
\echo '============================================================================'
\echo 'TEST 1: Single-row INSERT performance (10 transactions)'
\echo '============================================================================'

\echo ''
\echo '--- REGULAR TABLE ---'
INSERT INTO regular_table (test_id, data) VALUES (1, 'test');
INSERT INTO regular_table (test_id, data) VALUES (2, 'test');
INSERT INTO regular_table (test_id, data) VALUES (3, 'test');
INSERT INTO regular_table (test_id, data) VALUES (4, 'test');
INSERT INTO regular_table (test_id, data) VALUES (5, 'test');
INSERT INTO regular_table (test_id, data) VALUES (6, 'test');
INSERT INTO regular_table (test_id, data) VALUES (7, 'test');
INSERT INTO regular_table (test_id, data) VALUES (8, 'test');
INSERT INTO regular_table (test_id, data) VALUES (9, 'test');
INSERT INTO regular_table (test_id, data) VALUES (10, 'test');

\echo ''
\echo '--- BLOCKCHAIN TABLE ---'
INSERT INTO blockchain_table (test_id, data) VALUES (1, 'test');
INSERT INTO blockchain_table (test_id, data) VALUES (2, 'test');
INSERT INTO blockchain_table (test_id, data) VALUES (3, 'test');
INSERT INTO blockchain_table (test_id, data) VALUES (4, 'test');
INSERT INTO blockchain_table (test_id, data) VALUES (5, 'test');
INSERT INTO blockchain_table (test_id, data) VALUES (6, 'test');
INSERT INTO blockchain_table (test_id, data) VALUES (7, 'test');
INSERT INTO blockchain_table (test_id, data) VALUES (8, 'test');
INSERT INTO blockchain_table (test_id, data) VALUES (9, 'test');
INSERT INTO blockchain_table (test_id, data) VALUES (10, 'test');

\echo ''
\echo '============================================================================'
\echo 'TEST 2: Batch INSERT performance (1,000 rows)'
\echo '============================================================================'

\echo ''
\echo '--- REGULAR TABLE ---'
BEGIN;
INSERT INTO regular_table (test_id, data)
SELECT i, 'test_' || i
FROM generate_series(11, 1010) i;
COMMIT;

\echo ''
\echo '--- BLOCKCHAIN TABLE ---'
BEGIN;
INSERT INTO blockchain_table (test_id, data)
SELECT i, 'test_' || i
FROM generate_series(11, 1010) i;
COMMIT;

\echo ''
\echo '============================================================================'
\echo 'TEST 3: Large batch INSERT (10,000 rows)'
\echo '============================================================================'

\echo ''
\echo '--- REGULAR TABLE ---'
BEGIN;
INSERT INTO regular_table (test_id, data)
SELECT i, 'test_' || i
FROM generate_series(1011, 11010) i;
COMMIT;

\echo ''
\echo '--- BLOCKCHAIN TABLE ---'
BEGIN;
INSERT INTO blockchain_table (test_id, data)
SELECT i, 'test_' || i
FROM generate_series(1011, 11010) i;
COMMIT;

\echo ''
\echo '============================================================================'
\echo 'TEST 4: Row counts and verification'
\echo '============================================================================'

\echo ''
\echo '--- REGULAR TABLE ---'
SELECT COUNT(*) as total_rows FROM regular_table;

\echo ''
\echo '--- BLOCKCHAIN TABLE ---'
SELECT COUNT(*) as total_rows FROM blockchain_table;
SELECT * FROM verify_hash_chain('blockchain_table');

\echo ''
\echo '============================================================================'
\echo 'Summary: Regular table is faster because it skips:'
\echo '  - Counter allocation (atomic operations)'
\echo '  - SHA256 hash computation (expensive crypto)'
\echo '  - Previous hash lookup (cache/table lookup)'
\echo '  - Hash cache storage (shared memory write)'
\echo '  - Phantom block allocation (metadata tracking)'
\echo '============================================================================'
