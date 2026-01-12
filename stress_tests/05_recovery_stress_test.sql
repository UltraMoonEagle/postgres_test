-- ============================================================================
-- RECOVERY STRESS TEST
-- Tests phantom block recovery with large log files
-- Creates many rollbacks, restarts server, tests recovery performance
-- ============================================================================

\timing on
\echo '============================================================================'
\echo 'RECOVERY STRESS TEST - Large phantom log recovery'
\echo '============================================================================'
\echo ''

-- Create test table
DROP BLOCKCHAIN TABLE IF EXISTS stress_test_recovery CASCADE;
CREATE BLOCKCHAIN TABLE stress_test_recovery (
    test_id INT,
    data TEXT
);

\echo '=== Phase 1: Create mixed workload (commits + rollbacks) ==='
\echo 'Inserting 1000 committed rows...'
INSERT INTO stress_test_recovery
SELECT i, 'committed_' || i
FROM generate_series(1, 1000) i;

\echo 'Creating 500 phantom blocks...'
DO $$
DECLARE
    i INT;
BEGIN
    FOR i IN 1..500 LOOP
        BEGIN
            INSERT INTO stress_test_recovery VALUES (2000 + i, 'rollback_' || i);
            RAISE EXCEPTION 'Rollback';
        EXCEPTION
            WHEN OTHERS THEN NULL;
        END;
    END LOOP;
END $$;

\echo 'Inserting 1000 more committed rows...'
INSERT INTO stress_test_recovery
SELECT i, 'committed_' || i
FROM generate_series(1001, 2000) i;

\echo 'Creating 1000 more phantom blocks...'
DO $$
DECLARE
    i INT;
BEGIN
    FOR i IN 1..1000 LOOP
        BEGIN
            INSERT INTO stress_test_recovery VALUES (5000 + i, 'rollback_' || i);
            RAISE EXCEPTION 'Rollback';
        EXCEPTION
            WHEN OTHERS THEN NULL;
        END;
    END LOOP;
END $$;

\echo ''
\echo '=== Current State Before Restart ==='
SELECT COUNT(*) as committed_rows FROM stress_test_recovery;
SELECT COUNT(*) as phantom_blocks FROM blockchain_phantom_blocks
WHERE table_oid = 'stress_test_recovery'::regclass::oid;

\echo ''
\echo '=== Phantom Log File Size ==='
\! ls -lh ~/pgsql/data/global/blockchain_phantom.log

\echo ''
\echo '=== Hash Chain Verification Before Restart ==='
SELECT * FROM verify_hash_chain('stress_test_recovery');

\echo ''
\echo '============================================================================'
\echo 'MANUAL STEP REQUIRED:'
\echo 'To complete this test, you must:'
\echo '  1. Note the current phantom block count above'
\echo '  2. Restart the PostgreSQL server'
\echo '  3. Run the recovery verification script: 05b_verify_recovery.sql'
\echo '============================================================================'
