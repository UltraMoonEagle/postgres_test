#!/bin/bash
# ============================================================================
# Blockchain Table Concurrent Testing Script (Fixed Version)
# ============================================================================

echo "============================================================================"
echo "PostgreSQL Blockchain Table - Concurrent Testing Suite"
echo "============================================================================"

# Set PostgreSQL connection parameters
PGDATABASE=postgres

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}Setting up test environment...${NC}"

# Setup the blockchain_stress table
$HOME/pgsql/bin/psql -d $PGDATABASE -c "
DROP BLOCKCHAIN TABLE IF EXISTS blockchain_stress CASCADE;
CREATE BLOCKCHAIN TABLE blockchain_stress (
    thread_id INTEGER,
    iteration INTEGER,
    random_data TEXT,
    insert_timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
" > /dev/null 2>&1

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ Test environment ready${NC}"
else
    echo -e "${RED}✗ Failed to setup test environment${NC}"
    exit 1
fi

# Test 1: Light concurrent load
echo -e "${YELLOW}Test 1: Light Concurrent Load (3 clients, 20 transactions each)${NC}"

$HOME/pgsql/bin/pgbench -d $PGDATABASE -f pgbench_concurrent_test.sql -c 3 -j 3 -t 20 --no-vacuum > /tmp/pgbench_light.log 2>&1

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ Light concurrent test completed${NC}"
    
    # Show results
    $HOME/pgsql/bin/psql -d $PGDATABASE -c "
    SELECT 
        COUNT(*) as total_inserts,
        COUNT(DISTINCT __tx_lsn) as unique_counters,
        MIN(__tx_lsn) as min_counter,
        MAX(__tx_lsn) as max_counter,
        CASE WHEN COUNT(*) = COUNT(DISTINCT __tx_lsn) THEN 'PASS' ELSE 'FAIL' END as integrity_check
    FROM blockchain_stress;
    "
    
    # Show performance from log
    if [ -f /tmp/pgbench_light.log ]; then
        echo "Performance:"
        grep "tps" /tmp/pgbench_light.log | head -1
    fi
else
    echo -e "${RED}✗ Light concurrent test failed${NC}"
    cat /tmp/pgbench_light.log
fi

echo ""

# Test 2: Medium concurrent load
echo -e "${YELLOW}Test 2: Medium Concurrent Load (5 clients, 50 transactions each)${NC}"

$HOME/pgsql/bin/pgbench -d $PGDATABASE -f pgbench_concurrent_test.sql -c 5 -j 5 -t 50 --no-vacuum > /tmp/pgbench_medium.log 2>&1

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ Medium concurrent test completed${NC}"
    
    # Show cumulative results
    $HOME/pgsql/bin/psql -d $PGDATABASE -c "
    SELECT 
        COUNT(*) as total_inserts,
        COUNT(DISTINCT __tx_lsn) as unique_counters,
        CASE WHEN COUNT(*) = COUNT(DISTINCT __tx_lsn) THEN 'PASS' ELSE 'FAIL' END as integrity_check,
        COUNT(DISTINCT thread_id) as unique_clients
    FROM blockchain_stress;
    "
    
    # Show performance
    if [ -f /tmp/pgbench_medium.log ]; then
        echo "Performance:"
        grep "tps" /tmp/pgbench_medium.log | head -1
    fi
else
    echo -e "${RED}✗ Medium concurrent test failed${NC}"
    cat /tmp/pgbench_medium.log
fi

echo ""

# Test 3: Stress test (optional)
echo -e "${YELLOW}Test 3: Heavy Concurrent Load (10 clients, 100 transactions each = 1000 inserts)${NC}"
read -p "Continue with stress test? (y/N): " -n 1 -r
echo ""

if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "Running heavy concurrent stress test..."
    
    $HOME/pgsql/bin/pgbench -d $PGDATABASE -f pgbench_concurrent_test.sql -c 10 -j 10 -t 100 --no-vacuum --progress=50 > /tmp/pgbench_heavy.log 2>&1
    
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ Heavy concurrent test completed${NC}"
        
        # Comprehensive results
        $HOME/pgsql/bin/psql -d $PGDATABASE -c "
        SELECT 
            COUNT(*) as total_rows,
            COUNT(DISTINCT __tx_lsn) as unique_counters,
            MIN(__tx_lsn) as min_counter,
            MAX(__tx_lsn) as max_counter,
            CASE WHEN COUNT(*) = COUNT(DISTINCT __tx_lsn) THEN 'PASS: All unique' ELSE 'FAIL: Duplicates found' END as counter_integrity,
            COUNT(DISTINCT thread_id) as unique_clients,
            MIN(__tx_timestamp) as first_insert,
            MAX(__tx_timestamp) as last_insert
        FROM blockchain_stress;
        "
        
        # Show performance
        if [ -f /tmp/pgbench_heavy.log ]; then
            echo "Performance:"
            grep "tps" /tmp/pgbench_heavy.log | head -1
        fi
        
        # Hash chain verification
        echo -e "${BLUE}Verifying hash chain integrity...${NC}"
        $HOME/pgsql/bin/psql -d $PGDATABASE -c "
        WITH hash_check AS (
            SELECT 
                __tx_lsn,
                __prev_hash,
                LAG(__curr_hash) OVER (ORDER BY __tx_lsn) as expected_prev_hash
            FROM blockchain_stress
            ORDER BY __tx_lsn
        )
        SELECT 
            COUNT(*) FILTER (WHERE __tx_lsn > 1 AND __prev_hash != expected_prev_hash) as broken_links,
            CASE WHEN COUNT(*) FILTER (WHERE __tx_lsn > 1 AND __prev_hash != expected_prev_hash) = 0 
                 THEN 'PASS: Hash chain intact' 
                 ELSE 'FAIL: Broken hash chain' END as hash_integrity
        FROM hash_check;
        "
    else
        echo -e "${RED}✗ Heavy concurrent test failed${NC}"
        cat /tmp/pgbench_heavy.log
    fi
else
    echo "Skipping heavy concurrent test."
fi

echo ""
echo -e "${BLUE}Final Summary:${NC}"

# Final summary
$HOME/pgsql/bin/psql -d $PGDATABASE -c "
SELECT 
    'blockchain_stress' as table_name,
    COUNT(*) as total_records,
    pg_size_pretty(pg_total_relation_size('blockchain_stress')) as table_size,
    COUNT(DISTINCT thread_id) as unique_clients,
    MIN(__tx_timestamp) as first_record,
    MAX(__tx_timestamp) as last_record,
    CASE WHEN COUNT(*) = COUNT(DISTINCT __tx_lsn) THEN 'PERFECT' ELSE 'DUPLICATES FOUND' END as counter_integrity
FROM blockchain_stress;
"

echo ""
echo -e "${GREEN}Concurrent testing completed!${NC}"
echo -e "${BLUE}Logs saved in /tmp/pgbench_*.log${NC}"

# Cleanup temp files
rm -f /tmp/pgbench_*.log 2>/dev/null