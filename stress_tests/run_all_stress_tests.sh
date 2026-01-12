#!/bin/bash
# ============================================================================
# MASTER STRESS TEST RUNNER
# Runs all stress tests and generates comprehensive report
# ============================================================================

set -e

PSQL="$HOME/pgsql/bin/psql"
DB="postgres"
REPORT_FILE="stress_test_report_$(date +%Y%m%d_%H%M%S).txt"

echo "============================================================================"
echo "BLOCKCHAIN STRESS TEST SUITE"
echo "============================================================================"
echo ""
echo "This will run comprehensive stress tests to find system limits:"
echo "  1. Insert volume test (up to 100k rows)"
echo "  2. Hash cache overflow test (>10k uncommitted)"
echo "  3. Rollback stress test (high abort rates)"
echo "  4. Concurrent clients test (multiple writers)"
echo "  5. Recovery stress test (requires manual restart)"
echo ""
echo "Report will be saved to: $REPORT_FILE"
echo ""
read -p "Press ENTER to start tests..." dummy

# Start report
{
    echo "============================================================================"
    echo "BLOCKCHAIN STRESS TEST REPORT"
    echo "============================================================================"
    echo "Date: $(date)"
    echo "PostgreSQL Version: $($PSQL -d $DB -t -c 'SELECT version();')"
    echo ""

    # Test 1: Insert Volume
    echo "============================================================================"
    echo "TEST 1: INSERT VOLUME STRESS TEST"
    echo "============================================================================"
    $PSQL -d $DB -f 01_insert_volume_test.sql 2>&1

    echo ""
    echo "============================================================================"
    echo "TEST 2: HASH CACHE OVERFLOW TEST (>10k uncommitted)"
    echo "============================================================================"
    $PSQL -d $DB -f 02_hash_cache_overflow_test.sql 2>&1

    echo ""
    echo "============================================================================"
    echo "TEST 3: ROLLBACK STRESS TEST"
    echo "============================================================================"
    $PSQL -d $DB -f 03_rollback_stress_test.sql 2>&1

    echo ""
    echo "============================================================================"
    echo "TEST 4: CONCURRENT CLIENTS TEST - 4 clients"
    echo "============================================================================"
    chmod +x 04_concurrent_clients_test.sh
    ./04_concurrent_clients_test.sh 4 1000 2>&1

    echo ""
    echo "============================================================================"
    echo "TEST 4b: CONCURRENT CLIENTS TEST - 8 clients"
    echo "============================================================================"
    ./04_concurrent_clients_test.sh 8 1000 2>&1

    echo ""
    echo "============================================================================"
    echo "TEST 4c: CONCURRENT CLIENTS TEST - 16 clients"
    echo "============================================================================"
    ./04_concurrent_clients_test.sh 16 500 2>&1

    echo ""
    echo "============================================================================"
    echo "TEST 5: RECOVERY STRESS TEST (Setup)"
    echo "============================================================================"
    $PSQL -d $DB -f 05_recovery_stress_test.sql 2>&1

    echo ""
    echo "============================================================================"
    echo "STRESS TEST SUITE COMPLETE"
    echo "============================================================================"
    echo ""
    echo "All automated tests completed successfully!"
    echo ""
    echo "MANUAL STEP: To complete recovery test:"
    echo "  1. Restart PostgreSQL: ~/pgsql/bin/pg_ctl restart -D ~/pgsql/data -l ~/pgsql/data/logfile"
    echo "  2. Run: psql -d postgres -f 05b_verify_recovery.sql"
    echo ""
    echo "Report saved to: $REPORT_FILE"

} | tee "$REPORT_FILE"

echo ""
echo "Report saved to: $REPORT_FILE"
echo ""
echo "To view report: less $REPORT_FILE"
