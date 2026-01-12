-- ============================================================================
-- RECOVERY VERIFICATION
-- Run this AFTER restarting the server to verify recovery worked
-- ============================================================================

\timing on
\echo '============================================================================'
\echo 'RECOVERY VERIFICATION - After Server Restart'
\echo '============================================================================'
\echo ''

\echo '=== Phase 1: Check if table still exists ==='
SELECT COUNT(*) as committed_rows FROM stress_test_recovery;

\echo ''
\echo '=== Phase 2: Check phantom blocks table ==='
SELECT COUNT(*) as phantom_blocks_recovered FROM blockchain_phantom_blocks
WHERE table_oid = 'stress_test_recovery'::regclass::oid;

\echo ''
\echo '=== Phase 3: Check phantom log file (should be truncated/empty) ==='
\! ls -lh ~/pgsql/data/global/blockchain_phantom.log 2>/dev/null || echo "Log file not found (OK if recovery completed)"

\echo ''
\echo '=== Phase 4: Verify hash chain integrity after recovery ==='
SELECT * FROM verify_hash_chain('stress_test_recovery');

\echo ''
\echo '=== Phase 5: Sample phantom blocks ==='
SELECT counter, status
FROM blockchain_phantom_blocks
WHERE table_oid = 'stress_test_recovery'::regclass::oid
ORDER BY counter
LIMIT 20;

\echo ''
\echo '=== Phase 6: Verify no gaps in chain ==='
WITH combined AS (
    SELECT __tx_lsn as counter FROM stress_test_recovery
    UNION
    SELECT counter FROM blockchain_phantom_blocks
    WHERE table_oid = 'stress_test_recovery'::regclass::oid
),
gaps AS (
    SELECT
        counter,
        counter - LAG(counter, 1, counter - 1) OVER (ORDER BY counter) - 1 as gap_size
    FROM combined
)
SELECT
    CASE
        WHEN MAX(gap_size) = 0 THEN 'NO GAPS - Perfect recovery! ✓'
        ELSE 'GAPS FOUND - Recovery issue! ✗'
    END as recovery_status,
    COUNT(*) FILTER (WHERE gap_size > 0) as num_gaps,
    MAX(gap_size) as largest_gap
FROM gaps;

\echo ''
\echo 'Recovery verification complete!'
\echo 'If hash_chain shows 0 broken_links and no gaps found, recovery was perfect!'
