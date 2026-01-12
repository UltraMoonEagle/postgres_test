-- ============================================================================
-- ROLLBACK STRESS TEST
-- Tests system with high rollback rates and large phantom block logs
-- ============================================================================

\timing on
\echo '============================================================================'
\echo 'ROLLBACK STRESS TEST - Testing high abort rates'
\echo '============================================================================'
\echo ''

-- Create test table
DROP BLOCKCHAIN TABLE IF EXISTS stress_test_rollbacks CASCADE;
CREATE BLOCKCHAIN TABLE stress_test_rollbacks (
    test_id INT,
    data TEXT
);

\echo '=== Phase 1: 100 commits + 100 rollbacks (50% abort rate) ==='
-- Committed transactions
INSERT INTO stress_test_rollbacks
SELECT i, 'committed_' || i
FROM generate_series(1, 100) i;

-- Rolled back transactions (creates phantom blocks)
DO $$
DECLARE
    i INT;
BEGIN
    FOR i IN 1..100 LOOP
        BEGIN
            INSERT INTO stress_test_rollbacks VALUES (1000 + i, 'rollback_' || i);
            RAISE EXCEPTION 'Intentional rollback';
        EXCEPTION
            WHEN OTHERS THEN
                -- Rollback happens automatically
        END;
    END LOOP;
END $$;

SELECT COUNT(*) as committed_rows FROM stress_test_rollbacks;
SELECT COUNT(*) as phantom_blocks FROM blockchain_phantom_blocks
WHERE table_oid = 'stress_test_rollbacks'::regclass::oid;
\echo ''

\echo '=== Phase 2: 100 commits + 400 rollbacks (80% abort rate) ==='
-- More committed
INSERT INTO stress_test_rollbacks
SELECT i, 'committed_' || i
FROM generate_series(101, 200) i;

-- Many rollbacks
DO $$
DECLARE
    i INT;
BEGIN
    FOR i IN 1..400 LOOP
        BEGIN
            INSERT INTO stress_test_rollbacks VALUES (2000 + i, 'rollback_' || i);
            RAISE EXCEPTION 'Intentional rollback';
        EXCEPTION
            WHEN OTHERS THEN
                NULL;
        END;
    END LOOP;
END $$;

SELECT COUNT(*) as committed_rows FROM stress_test_rollbacks;
SELECT COUNT(*) as phantom_blocks FROM blockchain_phantom_blocks
WHERE table_oid = 'stress_test_rollbacks'::regclass::oid;
\echo ''

\echo '=== Phase 3: Test phantom log size ==='
\! ls -lh ~/pgsql/data/global/blockchain_phantom.log 2>/dev/null || echo "Log file not found"
\echo ''

\echo '=== Phase 4: 1000 rapid rollbacks (stress test phantom log) ==='
DO $$
DECLARE
    i INT;
BEGIN
    FOR i IN 1..1000 LOOP
        BEGIN
            INSERT INTO stress_test_rollbacks VALUES (3000 + i, 'rapid_rollback_' || i);
            RAISE EXCEPTION 'Rapid rollback';
        EXCEPTION
            WHEN OTHERS THEN
                NULL;
        END;
    END LOOP;
END $$;

\echo 'Check phantom log size after 1000 rollbacks:'
\! ls -lh ~/pgsql/data/global/blockchain_phantom.log
\echo ''

SELECT COUNT(*) as total_phantom_blocks FROM blockchain_phantom_blocks
WHERE table_oid = 'stress_test_rollbacks'::regclass::oid;

\echo '=== VERIFICATION: Hash chain with many phantom blocks ==='
SELECT * FROM verify_hash_chain('stress_test_rollbacks');

\echo ''
\echo '=== Sample of phantom blocks ==='
SELECT counter, status, encode(prev_hash, 'hex') as prev_hash_short
FROM blockchain_phantom_blocks
WHERE table_oid = 'stress_test_rollbacks'::regclass::oid
ORDER BY counter
LIMIT 20;

\echo ''
\echo 'Rollback stress test complete!'
