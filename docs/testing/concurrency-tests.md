# Concurrency Tests

## One-Minute Summary

Concurrency tests verify the blockchain extension handles multi-client workloads correctly:
- **Hash chain linearity**: No branching or forks under concurrent load
- **Counter atomicity**: No duplicate or skipped counter values
- **Race condition detection**: Proper synchronization between clients
- **Performance under load**: Throughput measurement with N concurrent clients

Uses **pgbench** to simulate 10-100 concurrent clients performing inserts. Critical for production readiness.

## Why Concurrency Testing Matters

### The Hash Branching Problem

Without proper concurrency control, blockchain tables can develop **forked hash chains**:

```
Expected (Linear Chain):
Block 1 → Block 2 → Block 3 → Block 4 → Block 5

Broken (Branched Chain):
         ┌→ Block 3
Block 1 → Block 2
         └→ Block 4 → Block 5
```

This breaks the fundamental blockchain guarantee: **every block has exactly one predecessor**.

### Historical Issue (Fixed)

**Before Fix** (See BLOCKCHAIN_CONCURRENCY_FIX.md):
- 10 clients, 100 transactions each = 1000 blocks
- Result: **3 genesis blocks**, **2 fork points**, **98.5% broken links**

**After Fix**:
- Same workload
- Result: **1 genesis block**, **0 fork points**, **100% perfect chain**
- Mechanism: Shared memory cache + retry loop with 2ms sleep

## Test Setup

### Prerequisites

```bash
# Verify pgbench is installed
which pgbench
# Expected: /usr/bin/pgbench (or similar)

# Check PostgreSQL is running
pg_isready
# Expected: accepting connections

# Verify extension loaded
psql -d testdb -c "SELECT * FROM pg_extension WHERE extname = 'blockchain_anchor';"
```

### Create Test Table

```sql
-- Drop existing test table
DROP TABLE IF EXISTS blockchain_stress CASCADE;

-- Create blockchain table for concurrency testing
CREATE BLOCKCHAIN TABLE blockchain_stress (
    thread_id INTEGER,
    iteration INTEGER,
    random_data TEXT,
    value NUMERIC,
    timestamp_recorded TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Verify creation
SELECT is_blockchain_table('blockchain_stress') AS is_bc;
-- Expected: t (true)
```

## Test Scripts

### Basic Concurrency Test

Create file: `pgbench_concurrent_test.sql`

```sql
INSERT INTO blockchain_stress (thread_id, iteration, random_data, value)
VALUES (
    :client_id,
    :scale,
    'pgbench_test_' || md5(random()::text),
    random() * 1000
);
```

### Run Basic Test

```bash
# 10 clients, 100 transactions each = 1000 total inserts
pgbench -d testdb \
    -c 10 \
    -j 4 \
    -t 100 \
    -f pgbench_concurrent_test.sql \
    -n

# Parameters explained:
# -c 10    : 10 concurrent clients
# -j 4     : 4 worker threads
# -t 100   : 100 transactions per client
# -f       : Script file to run
# -n       : No initialization (table already exists)
```

**Expected Output**:
```
starting vacuum...end.
transaction type: pgbench_concurrent_test.sql
scaling factor: 1
query mode: simple
number of clients: 10
number of threads: 4
number of transactions per client: 100
number of transactions actually processed: 1000/1000
latency average = 8.234 ms
tps = 881.123456 (including connections establishing)
tps = 902.345678 (excluding connections establishing)
```

## Verification Queries

### 1. Hash Chain Integrity Check

```sql
-- Complete hash chain analysis
WITH hash_chain AS (
    SELECT
        __tx_lsn,
        __curr_hash,
        __prev_hash,
        LAG(__curr_hash) OVER (ORDER BY __tx_lsn) AS expected_prev_hash
    FROM blockchain_stress
),
chain_stats AS (
    SELECT
        COUNT(*) AS total_blocks,
        COUNT(*) FILTER (
            WHERE __prev_hash = '\x0000000000000000000000000000000000000000000000000000000000000000'
        ) AS genesis_blocks,
        COUNT(*) FILTER (
            WHERE __tx_lsn > 1 AND __prev_hash != expected_prev_hash
        ) AS broken_links,
        COUNT(DISTINCT __prev_hash) FILTER (WHERE __tx_lsn > 1) AS unique_prev_hashes,
        COUNT(*) - 1 AS expected_unique_prev_hashes
    FROM hash_chain
)
SELECT
    total_blocks,
    genesis_blocks,
    broken_links,
    unique_prev_hashes,
    expected_unique_prev_hashes,
    CASE
        WHEN genesis_blocks = 1 AND broken_links = 0 THEN 'PASS'
        ELSE 'FAIL'
    END AS chain_status
FROM chain_stats;
```

**Expected Result**:
```
 total_blocks | genesis_blocks | broken_links | unique_prev_hashes | expected_unique_prev_hashes | chain_status
--------------+----------------+--------------+--------------------+-----------------------------+--------------
         1000 |              1 |            0 |                999 |                         999 | PASS
```

### 2. Counter Sequence Verification

```sql
-- Verify counters are unique and sequential
SELECT
    MIN(__tx_lsn) AS first_counter,
    MAX(__tx_lsn) AS last_counter,
    COUNT(*) AS total_rows,
    COUNT(DISTINCT __tx_lsn) AS unique_counters,
    MAX(__tx_lsn) - MIN(__tx_lsn) + 1 AS expected_counters,
    CASE
        WHEN COUNT(*) = COUNT(DISTINCT __tx_lsn)
         AND COUNT(*) = MAX(__tx_lsn) - MIN(__tx_lsn) + 1
        THEN 'PASS'
        ELSE 'FAIL'
    END AS counter_status
FROM blockchain_stress;
```

**Expected Result**:
```
 first_counter | last_counter | total_rows | unique_counters | expected_counters | counter_status
---------------+--------------+------------+-----------------+-------------------+----------------
             1 |         1000 |       1000 |            1000 |              1000 | PASS
```

### 3. Fork Point Detection

```sql
-- Find any fork points (multiple blocks with same prev_hash)
WITH fork_analysis AS (
    SELECT
        __prev_hash,
        COUNT(*) AS branches
    FROM blockchain_stress
    WHERE __tx_lsn > 1  -- Exclude genesis
    GROUP BY __prev_hash
    HAVING COUNT(*) > 1
)
SELECT
    encode(__prev_hash, 'hex') AS forked_prev_hash,
    branches,
    'FAIL: Fork detected!' AS status
FROM fork_analysis
UNION ALL
SELECT
    'No forks detected' AS message,
    0 AS branches,
    'PASS' AS status
WHERE NOT EXISTS (SELECT 1 FROM fork_analysis);
```

**Expected Result**:
```
 forked_prev_hash   | branches | status
--------------------+----------+--------
 No forks detected  |        0 | PASS
```

### 4. Per-Client Distribution

```sql
-- Verify all clients participated
SELECT
    thread_id,
    COUNT(*) AS transactions,
    MIN(__tx_lsn) AS first_counter,
    MAX(__tx_lsn) AS last_counter
FROM blockchain_stress
GROUP BY thread_id
ORDER BY thread_id;
```

**Expected Result** (with 10 clients, 100 txns each):
```
 thread_id | transactions | first_counter | last_counter
-----------+--------------+---------------+--------------
         0 |          100 |             3 |          997
         1 |          100 |             1 |          995
         2 |          100 |             2 |          999
         3 |          100 |             5 |          998
         ... (all 10 clients)
```

## Advanced Concurrency Tests

### High Contention Test

```bash
# 50 clients, 200 transactions each = 10,000 total
pgbench -d testdb \
    -c 50 \
    -j 8 \
    -t 200 \
    -f pgbench_concurrent_test.sql \
    -n \
    -P 5  # Progress report every 5 seconds

# Expected TPS: 400-800 depending on hardware
```

### Sustained Load Test

```bash
# Run for 60 seconds with 20 clients
pgbench -d testdb \
    -c 20 \
    -j 4 \
    -T 60 \
    -f pgbench_concurrent_test.sql \
    -n \
    -P 10

# -T 60: Run for 60 seconds instead of fixed transaction count
```

### Mixed Workload Test

Create file: `mixed_workload.sql`

```sql
-- 80% inserts, 20% reads
\set insert_prob 80

SELECT CASE
    WHEN random() * 100 < :insert_prob THEN (
        INSERT INTO blockchain_stress (thread_id, iteration, random_data, value)
        VALUES (:client_id, :scale, 'mixed_' || md5(random()::text), random() * 1000)
    )
    ELSE (
        SELECT COUNT(*) FROM blockchain_stress WHERE thread_id = :client_id
    )
END;
```

Run mixed workload:
```bash
pgbench -d testdb -c 20 -j 4 -T 30 -f mixed_workload.sql -n
```

## Visual Hash Chain Verification

### Generate Chain Visualization

```sql
-- Export hash chain for visualization
COPY (
    SELECT
        __tx_lsn AS seq,
        encode(__curr_hash, 'hex') AS curr_hash,
        encode(__prev_hash, 'hex') AS prev_hash,
        thread_id,
        __tx_timestamp
    FROM blockchain_stress
    ORDER BY __tx_lsn
) TO '/tmp/hash_chain_export.csv' CSV HEADER;
```

### Python Visualization Script

Create file: `visualize_chain.py`

```python
#!/usr/bin/env python3
import csv
import sys

def visualize_chain(csv_file):
    """Visualize blockchain hash chain from CSV export."""

    blocks = []
    with open(csv_file, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            blocks.append({
                'seq': int(row['seq']),
                'curr': row['curr_hash'][:8],  # First 8 chars
                'prev': row['prev_hash'][:8],
                'thread': row['thread_id']
            })

    print(f"Total blocks: {len(blocks)}")

    # Check genesis
    genesis_count = sum(1 for b in blocks if b['prev'] == '00000000')
    print(f"Genesis blocks: {genesis_count}")

    # Check for forks
    prev_hashes = {}
    for b in blocks[1:]:  # Skip genesis
        if b['prev'] not in prev_hashes:
            prev_hashes[b['prev']] = []
        prev_hashes[b['prev']].append(b['seq'])

    forks = {k: v for k, v in prev_hashes.items() if len(v) > 1}
    print(f"Fork points: {len(forks)}")

    if forks:
        print("Fork details:")
        for prev_hash, sequences in forks.items():
            print(f"  Hash {prev_hash} has {len(sequences)} children: {sequences}")

    # Check chain integrity
    blocks_by_seq = {b['seq']: b for b in blocks}
    breaks = 0
    for b in blocks[1:]:
        prev_block = blocks_by_seq.get(b['seq'] - 1)
        if prev_block and b['prev'] != prev_block['curr']:
            breaks += 1
            print(f"BREAK at seq {b['seq']}: expected {prev_block['curr']}, got {b['prev']}")

    print(f"Broken hash links: {breaks}")

    # Visual representation (first 20 blocks)
    print("\nFirst 20 blocks:")
    for b in blocks[:20]:
        prefix = "    "
        if b['prev'] == '00000000':
            prefix = ">>> "  # Genesis
        print(f"{prefix}[{b['seq']:4d}] {b['curr']} <- {b['prev']} (thread {b['thread']})")

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <hash_chain_export.csv>")
        sys.exit(1)
    visualize_chain(sys.argv[1])
```

Run visualization:
```bash
python3 visualize_chain.py /tmp/hash_chain_export.csv
```

**Expected Output**:
```
Total blocks: 1000
Genesis blocks: 1
Fork points: 0
Broken hash links: 0

First 20 blocks:
>>> [   1] a7f8e9d2 <- 00000000 (thread 1)
    [   2] 3c2d1bf4 <- a7f8e9d2 (thread 3)
    [   3] 9e4f2ac7 <- 3c2d1bf4 (thread 0)
    [   4] 5b6c8de1 <- 9e4f2ac7 (thread 2)
    ...
```

## Performance Metrics

### Key Metrics to Track

| Metric | Description | Target |
|--------|-------------|--------|
| TPS | Transactions per second | > 500 |
| Latency (avg) | Average transaction time | < 10ms |
| Latency (p95) | 95th percentile latency | < 25ms |
| Hash Chain Integrity | % of correct links | 100% |
| Genesis Blocks | Count of genesis blocks | 1 |
| Fork Points | Count of hash chain branches | 0 |

### Capture Detailed Metrics

```bash
# Run with detailed output
pgbench -d testdb \
    -c 10 \
    -j 4 \
    -t 100 \
    -f pgbench_concurrent_test.sql \
    -n \
    -l \
    --log-prefix=blockchain_bench \
    -P 5

# Analyze latency distribution
cat blockchain_bench_*.*.gz | gunzip | \
    awk '{sum+=$3; sumsq+=$3*$3; n++} END {print "Avg:", sum/n, "ms | StdDev:", sqrt(sumsq/n - (sum/n)^2), "ms"}'
```

## Troubleshooting

### Hash Chain Breaks Detected

**Symptom**: `broken_links > 0` in verification query

**Diagnosis**:
```sql
-- Find exact break points
WITH breaks AS (
    SELECT
        __tx_lsn,
        encode(__curr_hash, 'hex') AS curr,
        encode(__prev_hash, 'hex') AS prev,
        encode(LAG(__curr_hash) OVER (ORDER BY __tx_lsn), 'hex') AS expected_prev
    FROM blockchain_stress
)
SELECT * FROM breaks
WHERE __tx_lsn > 1 AND prev != expected_prev
LIMIT 10;
```

**Possible Causes**:
1. Retry loop disabled or timeout too short
2. Shared memory cache not initialized
3. Counter race condition
4. High system load causing timeout

**Fix**: Check `get_previous_hash()` in `blockchainam.c` - ensure retry loop is active with 500 iterations × 2ms sleep.

### Multiple Genesis Blocks

**Symptom**: `genesis_blocks > 1`

**Cause**: Multiple transactions incorrectly using zero hash as previous hash.

**Fix**: Verify counter starts at 1 and first block check is correct in `get_previous_hash()`.

### Low TPS Performance

**Symptom**: TPS < 100

**Diagnosis**:
```sql
-- Check for lock contention
SELECT
    relation::regclass,
    mode,
    COUNT(*) AS lock_count
FROM pg_locks
WHERE relation IN (SELECT oid FROM pg_class WHERE relname = 'blockchain_stress')
GROUP BY relation, mode;

-- Check shared memory usage
SELECT * FROM pg_shmem_allocations WHERE name LIKE '%blockchain%';
```

**Possible Causes**:
1. Insufficient `shared_buffers` or `work_mem`
2. Disk I/O bottleneck
3. Lock contention on counter or cache
4. Too many retries in `get_previous_hash()`

**Fix**:
```sql
-- Increase PostgreSQL memory settings (postgresql.conf)
shared_buffers = 2GB
work_mem = 64MB
maintenance_work_mem = 512MB

-- Restart PostgreSQL
```

## Best Practices

1. **Baseline First**: Run single-client test before concurrency test
2. **Incremental Load**: Start with 5 clients, then 10, 20, 50
3. **Monitor Resources**: Watch CPU, memory, disk I/O during test
4. **Clean Slate**: Drop and recreate table between major test runs
5. **Verify After Each Run**: Always run hash chain integrity check
6. **Document Environment**: Record hardware specs and PostgreSQL config

## Example Test Session

```bash
# 1. Setup
psql -d testdb -c "DROP TABLE IF EXISTS blockchain_stress;"
psql -d testdb -c "CREATE BLOCKCHAIN TABLE blockchain_stress (thread_id INT, iteration INT, random_data TEXT, value NUMERIC);"

# 2. Baseline (single client)
pgbench -d testdb -c 1 -t 100 -f pgbench_concurrent_test.sql -n
# Expected TPS: 800-1200

# 3. Low concurrency (10 clients)
pgbench -d testdb -c 10 -t 100 -f pgbench_concurrent_test.sql -n -P 5
# Expected TPS: 500-1000

# 4. High concurrency (50 clients)
pgbench -d testdb -c 50 -t 200 -f pgbench_concurrent_test.sql -n -P 5
# Expected TPS: 400-800

# 5. Verify integrity
psql -d testdb -f verify_hash_chain.sql
# Expected: 100% integrity, 0 breaks, 1 genesis

# 6. Export for visualization
psql -d testdb -c "COPY (SELECT __tx_lsn, encode(__curr_hash,'hex'), encode(__prev_hash,'hex') FROM blockchain_stress ORDER BY __tx_lsn) TO '/tmp/chain.csv' CSV;"
python3 visualize_chain.py /tmp/chain.csv
```

## Next Steps

After concurrency tests pass:
1. Run [Performance Benchmarks](performance.md) for detailed profiling
2. Review [Security Documentation](../security/index.md) for production hardening
3. Consult [Concurrency Fix Documentation](/BLOCKCHAIN_CONCURRENCY_FIX.md) for implementation details

## References

- [pgbench Documentation](https://www.postgresql.org/docs/current/pgbench.html)
- [Blockchain Concurrency Fix](../../BLOCKCHAIN_CONCURRENCY_FIX.md)
- [Performance Tuning](performance.md)
- [Architecture Overview](../architecture/index.md)
