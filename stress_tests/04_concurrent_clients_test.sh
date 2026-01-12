#!/bin/bash
# ============================================================================
# CONCURRENT CLIENTS STRESS TEST
# Tests multiple clients inserting simultaneously
# ============================================================================

set -e

PSQL="$HOME/pgsql/bin/psql"
DB="postgres"
NUM_CLIENTS=${1:-4}  # Default to 4 clients
ROWS_PER_CLIENT=${2:-1000}  # Default to 1000 rows per client

echo "============================================================================"
echo "CONCURRENT CLIENTS STRESS TEST"
echo "============================================================================"
echo ""
echo "Configuration:"
echo "  - Number of clients: $NUM_CLIENTS"
echo "  - Rows per client: $ROWS_PER_CLIENT"
echo "  - Total expected rows: $((NUM_CLIENTS * ROWS_PER_CLIENT))"
echo ""

# Create test table
echo "=== Setting up test table ==="
$PSQL -d $DB << EOF
DROP BLOCKCHAIN TABLE IF EXISTS stress_test_concurrent CASCADE;
CREATE BLOCKCHAIN TABLE stress_test_concurrent (
    client_id INT,
    row_num INT,
    data TEXT,
    inserted_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
EOF

# Function to run inserts for a single client
run_client() {
    local client_id=$1
    local start_time=$(date +%s.%N)

    $PSQL -d $DB -q << EOF
DO \$\$
DECLARE
    i INT;
BEGIN
    FOR i IN 1..$ROWS_PER_CLIENT LOOP
        INSERT INTO stress_test_concurrent (client_id, row_num, data)
        VALUES ($client_id, i, 'client_${client_id}_row_' || i);
    END LOOP;
END \$\$;
EOF

    local end_time=$(date +%s.%N)
    local duration=$(echo "$end_time - $start_time" | bc)
    echo "Client $client_id: Completed $ROWS_PER_CLIENT inserts in ${duration}s"
}

# Launch all clients in parallel
echo ""
echo "=== Phase 1: Launching $NUM_CLIENTS concurrent clients ==="
START_TIME=$(date +%s)

for ((i=1; i<=NUM_CLIENTS; i++)); do
    run_client $i &
done

# Wait for all clients to complete
echo "Waiting for all clients to complete..."
wait

END_TIME=$(date +%s)
TOTAL_TIME=$((END_TIME - START_TIME))

echo ""
echo "=== All clients completed in ${TOTAL_TIME}s ==="

# Verify results
echo ""
echo "=== Phase 2: Verifying results ==="
$PSQL -d $DB << EOF
\timing on

-- Count total rows
SELECT COUNT(*) as total_rows FROM stress_test_concurrent;

-- Count rows per client
SELECT client_id, COUNT(*) as rows
FROM stress_test_concurrent
GROUP BY client_id
ORDER BY client_id;

-- Check for any duplicate counters (should be none!)
SELECT __tx_lsn, COUNT(*) as duplicates
FROM stress_test_concurrent
GROUP BY __tx_lsn
HAVING COUNT(*) > 1;

-- Verify hash chain integrity
SELECT * FROM verify_hash_chain('stress_test_concurrent');

-- Show some statistics
SELECT
    MIN(__tx_lsn) as min_counter,
    MAX(__tx_lsn) as max_counter,
    MAX(__tx_lsn) - MIN(__tx_lsn) + 1 as counter_range,
    COUNT(*) as actual_rows,
    CASE
        WHEN MAX(__tx_lsn) - MIN(__tx_lsn) + 1 = COUNT(*)
        THEN 'NO GAPS ✓'
        ELSE 'GAPS DETECTED ✗'
    END as gap_status
FROM stress_test_concurrent;

EOF

echo ""
echo "=== Performance Summary ==="
echo "Total time: ${TOTAL_TIME}s"
echo "Total rows: $((NUM_CLIENTS * ROWS_PER_CLIENT))"
echo "Throughput: $(echo "scale=2; ($NUM_CLIENTS * $ROWS_PER_CLIENT) / $TOTAL_TIME" | bc) rows/sec"
echo ""
echo "Concurrent clients test complete!"
