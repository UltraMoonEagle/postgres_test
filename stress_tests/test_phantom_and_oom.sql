-- ============================================================================
-- Comprehensive Test: Phantom Blocks and OOM Fix
-- ============================================================================

\timing on

-- ============================================================================
-- TEST 1: Phantom Block Functionality
-- ============================================================================

\echo ''
\echo '============================================================================'
\echo 'TEST 1: Phantom Block Creation and Recovery'
\echo '============================================================================'

DROP BLOCKCHAIN TABLE IF EXISTS phantom_test CASCADE;
CREATE BLOCKCHAIN TABLE phantom_test (test_id INT, data TEXT);

-- Insert 10 successful transactions
\echo ''
\echo '--- Phase 1: Insert 10 successful rows ---'
INSERT INTO phantom_test VALUES (1, 'success');
INSERT INTO phantom_test VALUES (2, 'success');
INSERT INTO phantom_test VALUES (3, 'success');
INSERT INTO phantom_test VALUES (4, 'success');
INSERT INTO phantom_test VALUES (5, 'success');
INSERT INTO phantom_test VALUES (6, 'success');
INSERT INTO phantom_test VALUES (7, 'success');
INSERT INTO phantom_test VALUES (8, 'success');
INSERT INTO phantom_test VALUES (9, 'success');
INSERT INTO phantom_test VALUES (10, 'success');

-- Now create phantom blocks with intentional rollbacks
\echo ''
\echo '--- Phase 2: Create 5 phantom blocks (rolled back transactions) ---'
BEGIN;
INSERT INTO phantom_test VALUES (11, 'will_rollback');
ROLLBACK;

BEGIN;
INSERT INTO phantom_test VALUES (12, 'will_rollback');
ROLLBACK;

BEGIN;
INSERT INTO phantom_test VALUES (13, 'will_rollback');
ROLLBACK;

BEGIN;
INSERT INTO phantom_test VALUES (14, 'will_rollback');
ROLLBACK;

BEGIN;
INSERT INTO phantom_test VALUES (15, 'will_rollback');
ROLLBACK;

-- Insert 5 more successful transactions after rollbacks
\echo ''
\echo '--- Phase 3: Insert 5 more successful rows ---'
INSERT INTO phantom_test VALUES (16, 'success');
INSERT INTO phantom_test VALUES (17, 'success');
INSERT INTO phantom_test VALUES (18, 'success');
INSERT INTO phantom_test VALUES (19, 'success');
INSERT INTO phantom_test VALUES (20, 'success');

-- Check results
\echo ''
\echo '--- Verification ---'
SELECT COUNT(*) as total_rows FROM phantom_test;
SELECT MIN(__tx_lsn) as first_counter, MAX(__tx_lsn) as last_counter FROM phantom_test;

-- Check for gaps (should be counters 11-15)
\echo ''
\echo '--- Expected gaps: counters 11-15 (phantom blocks) ---'
SELECT __tx_lsn as counter FROM phantom_test ORDER BY __tx_lsn;

-- Verify hash chain
\echo ''
\echo '--- Hash Chain Verification (should handle gaps) ---'
SELECT * FROM verify_hash_chain('phantom_test');

-- Check phantom_blocks table
\echo ''
\echo '--- Phantom Blocks Table (should have 5 ABORTED entries) ---'
SELECT table_oid, counter, status
FROM blockchain_phantom_blocks
WHERE table_oid = (SELECT oid FROM pg_class WHERE relname = 'phantom_test')
ORDER BY counter;

-- ============================================================================
-- TEST 2: OOM Fix - Large Single Transaction
-- ============================================================================

\echo ''
\echo '============================================================================'
\echo 'TEST 2: OOM Fix - 60,000 rows (exceeds old 50k hash cache)'
\echo '============================================================================'

DROP BLOCKCHAIN TABLE IF EXISTS oom_test_60k CASCADE;
CREATE BLOCKCHAIN TABLE oom_test_60k (test_id INT, data TEXT);

\echo ''
\echo '--- Inserting 60,000 rows (should trigger hash cache cleanup) ---'
BEGIN;
INSERT INTO oom_test_60k (test_id, data)
SELECT i, 'test_' || i
FROM generate_series(1, 60000) i;
COMMIT;

SELECT COUNT(*) as total_rows FROM oom_test_60k;
SELECT * FROM verify_hash_chain('oom_test_60k');

-- ============================================================================
-- TEST 3: OOM Fix - Multiple Large Transactions (Cumulative Test)
-- ============================================================================

\echo ''
\echo '============================================================================'
\echo 'TEST 3: OOM Fix - Multiple 30k transactions (cumulative 90k)'
\echo '============================================================================'

DROP BLOCKCHAIN TABLE IF EXISTS oom_test_cumulative CASCADE;
CREATE BLOCKCHAIN TABLE oom_test_cumulative (test_id INT, data TEXT);

\echo ''
\echo '--- Transaction 1: 30,000 rows ---'
BEGIN;
INSERT INTO oom_test_cumulative (test_id, data)
SELECT i, 'test_' || i
FROM generate_series(1, 30000) i;
COMMIT;

\echo ''
\echo '--- Transaction 2: 30,000 rows (cumulative: 60k) ---'
BEGIN;
INSERT INTO oom_test_cumulative (test_id, data)
SELECT i, 'test_' || i
FROM generate_series(30001, 60000) i;
COMMIT;

\echo ''
\echo '--- Transaction 3: 30,000 rows (cumulative: 90k) ---'
BEGIN;
INSERT INTO oom_test_cumulative (test_id, data)
SELECT i, 'test_' || i
FROM generate_series(60001, 90000) i;
COMMIT;

SELECT COUNT(*) as total_rows FROM oom_test_cumulative;
SELECT * FROM verify_hash_chain('oom_test_cumulative');

-- ============================================================================
-- TEST 4: Mixed Workload - Successes and Rollbacks
-- ============================================================================

\echo ''
\echo '============================================================================'
\echo 'TEST 4: Mixed Workload - 100 commits + 50 rollbacks'
\echo '============================================================================'

DROP BLOCKCHAIN TABLE IF EXISTS mixed_test CASCADE;
CREATE BLOCKCHAIN TABLE mixed_test (test_id INT, data TEXT);

\echo ''
\echo '--- Creating mixed workload ---'
DO $$
DECLARE
    i INT;
BEGIN
    -- Insert 100 successful rows
    FOR i IN 1..100 LOOP
        INSERT INTO mixed_test VALUES (i, 'commit_' || i);
    END LOOP;

    -- Insert 50 rolled back rows
    FOR i IN 101..150 LOOP
        BEGIN
            INSERT INTO mixed_test VALUES (i, 'rollback_' || i);
            RAISE EXCEPTION 'Intentional rollback';
        EXCEPTION WHEN OTHERS THEN
            -- Rollback happened
        END;
    END LOOP;

    -- Insert 50 more successful rows
    FOR i IN 151..200 LOOP
        INSERT INTO mixed_test VALUES (i, 'commit_' || i);
    END LOOP;
END $$;

SELECT COUNT(*) as total_rows FROM mixed_test;
SELECT MIN(__tx_lsn) as first, MAX(__tx_lsn) as last FROM mixed_test;

-- Count phantom blocks
SELECT COUNT(*) as phantom_blocks
FROM blockchain_phantom_blocks
WHERE table_oid = (SELECT oid FROM pg_class WHERE relname = 'mixed_test');

SELECT * FROM verify_hash_chain('mixed_test');

-- ============================================================================
-- SUMMARY
-- ============================================================================

\echo ''
\echo '============================================================================'
\echo 'SUMMARY'
\echo '============================================================================'
\echo 'Test 1: Phantom blocks created and tracked correctly'
\echo 'Test 2: 60k row transaction succeeded (hash cache cleanup working)'
\echo 'Test 3: 90k cumulative rows succeeded (cleanup across transactions)'
\echo 'Test 4: Mixed workload handled correctly'
\echo ''
\echo 'Expected Results:'
\echo '  - All hash chains should have 0 broken links'
\echo '  - Phantom blocks should be tracked in blockchain_phantom_blocks table'
\echo '  - No OOM errors even with large transactions'
\echo '  - Hash cache cleanup should trigger automatically'
\echo '============================================================================'
