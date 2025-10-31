# Blockchain Chain Verification Examples

**Difficulty**: Intermediate to Advanced
**Time to Complete**: 20-30 minutes
**Prerequisites**: PostgreSQL with blockchain extension, sample blockchain table with data

## Overview

This guide provides comprehensive techniques for verifying blockchain table integrity, detecting chain breaks, analyzing gaps, and diagnosing issues. These verification methods are essential for audit compliance and ensuring data immutability.

## Setup Test Environment

### Create Test Table and Data

```sql
-- Create a blockchain table for testing
CREATE BLOCKCHAIN TABLE verification_test (
    transaction_id SERIAL,
    description TEXT,
    amount DECIMAL(10,2),
    timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Insert test data
INSERT INTO verification_test (description, amount)
SELECT
    'Transaction ' || generate_series,
    (random() * 1000)::DECIMAL(10,2)
FROM generate_series(1, 100);
```

**Expected Output**:
```
CREATE TABLE
INSERT 0 100
```

## Verification Method 1: Basic Hash Chain Validation

### Check Genesis Block

```sql
-- Verify exactly one genesis block exists
SELECT
    COUNT(*) as genesis_count,
    CASE
        WHEN COUNT(*) = 1 THEN 'PASS'
        WHEN COUNT(*) = 0 THEN 'FAIL: No genesis block'
        ELSE 'FAIL: Multiple genesis blocks (' || COUNT(*) || ')'
    END as status
FROM verification_test
WHERE __prev_hash = '\x0000000000000000000000000000000000000000000000000000000000000000';
```

**Expected Output**:
```
 genesis_count | status
---------------+--------
             1 | PASS
(1 row)
```

**Explanation**: Every blockchain should have exactly one genesis block (the first block with prev_hash = all zeros).

### Check Hash Linkage

```sql
-- Verify every non-genesis block links to an existing hash
SELECT
    COUNT(*) as orphan_blocks,
    CASE
        WHEN COUNT(*) = 0 THEN 'PASS: All blocks properly linked'
        ELSE 'FAIL: Found ' || COUNT(*) || ' orphan blocks'
    END as status
FROM verification_test v1
WHERE v1.__prev_hash != '\x0000000000000000000000000000000000000000000000000000000000000000'
  AND NOT EXISTS (
      SELECT 1
      FROM verification_test v2
      WHERE v2.__curr_hash = v1.__prev_hash
  );
```

**Expected Output**:
```
 orphan_blocks |            status
---------------+------------------------------
             0 | PASS: All blocks properly linked
(1 row)
```

**Time**: ~50ms for 100 blocks

**Explanation**: Every block (except genesis) must point to an existing previous hash.

### Recursive Chain Traversal

```sql
-- Traverse the entire chain from genesis to tip
WITH RECURSIVE hash_chain AS (
    -- Base case: Genesis block
    SELECT
        __tx_lsn,
        __curr_hash,
        __prev_hash,
        1 as depth,
        description
    FROM verification_test
    WHERE __prev_hash = '\x0000000000000000000000000000000000000000000000000000000000000000'

    UNION ALL

    -- Recursive case: Follow the chain
    SELECT
        v.__tx_lsn,
        v.__curr_hash,
        v.__prev_hash,
        hc.depth + 1,
        v.description
    FROM verification_test v
    INNER JOIN hash_chain hc ON v.__prev_hash = hc.__curr_hash
)
SELECT
    (SELECT COUNT(*) FROM verification_test) as total_blocks,
    COUNT(*) as reachable_blocks,
    MAX(depth) as chain_length,
    (SELECT COUNT(*) FROM verification_test) - COUNT(*) as broken_links,
    CASE
        WHEN (SELECT COUNT(*) FROM verification_test) = COUNT(*) THEN 'PASS'
        ELSE 'FAIL'
    END as integrity_status
FROM hash_chain;
```

**Expected Output**:
```
 total_blocks | reachable_blocks | chain_length | broken_links | integrity_status
--------------+------------------+--------------+--------------+------------------
          100 |              100 |          100 |            0 | PASS
(1 row)
```

**Time**: ~100ms for 100 blocks

**Explanation**: This recursively follows the chain from genesis. If all blocks are reachable, the chain is intact.

## Verification Method 2: Counter Integrity

### Check Counter Uniqueness

```sql
-- Verify all counters are unique
SELECT
    COUNT(*) as total_records,
    COUNT(DISTINCT __tx_lsn) as unique_counters,
    COUNT(*) - COUNT(DISTINCT __tx_lsn) as duplicates,
    CASE
        WHEN COUNT(*) = COUNT(DISTINCT __tx_lsn) THEN 'PASS'
        ELSE 'FAIL: Found ' || (COUNT(*) - COUNT(DISTINCT __tx_lsn)) || ' duplicates'
    END as status
FROM verification_test;
```

**Expected Output**:
```
 total_records | unique_counters | duplicates | status
---------------+-----------------+------------+--------
           100 |             100 |          0 | PASS
(1 row)
```

### Check Counter Sequentiality

```sql
-- Check for gaps in counter sequence
WITH counter_analysis AS (
    SELECT
        __tx_lsn as current_counter,
        LAG(__tx_lsn) OVER (ORDER BY __tx_lsn) as prev_counter,
        __tx_lsn - LAG(__tx_lsn) OVER (ORDER BY __tx_lsn) as gap
    FROM verification_test
)
SELECT
    MIN(current_counter) as first_counter,
    MAX(current_counter) as last_counter,
    COUNT(*) as total_blocks,
    COUNT(*) FILTER (WHERE gap = 1) as sequential_blocks,
    COUNT(*) FILTER (WHERE gap > 1) as gap_count,
    COALESCE(MAX(gap), 0) as max_gap,
    CASE
        WHEN COUNT(*) FILTER (WHERE gap > 1) = 0 THEN 'PASS: Perfect sequence'
        ELSE 'INFO: Found ' || COUNT(*) FILTER (WHERE gap > 1) || ' gaps (expected in concurrent operations)'
    END as status
FROM counter_analysis
WHERE prev_counter IS NOT NULL;
```

**Expected Output** (no concurrent load):
```
 first_counter | last_counter | total_blocks | sequential_blocks | gap_count | max_gap |         status
---------------+--------------+--------------+-------------------+-----------+---------+------------------------
             1 |          100 |           99 |                99 |         0 |       1 | PASS: Perfect sequence
(1 row)
```

**Expected Output** (with concurrent load):
```
 first_counter | last_counter | total_blocks | sequential_blocks | gap_count | max_gap |                    status
---------------+--------------+--------------+-------------------+-----------+---------+-----------------------------------------------
             1 |          150 |          149 |               120 |        29 |       5 | INFO: Found 29 gaps (expected in concurrent operations)
(1 row)
```

**Explanation**: Gaps in counters are normal during concurrent operations when transactions abort. What matters is that hashes still link correctly.

### Identify Specific Gaps

```sql
-- List all gaps in counter sequence
WITH counter_gaps AS (
    SELECT
        __tx_lsn as current_counter,
        LAG(__tx_lsn) OVER (ORDER BY __tx_lsn) as prev_counter,
        __tx_lsn - LAG(__tx_lsn) OVER (ORDER BY __tx_lsn) as gap_size
    FROM verification_test
)
SELECT
    prev_counter as counter_before_gap,
    current_counter as counter_after_gap,
    gap_size,
    gap_size - 1 as missing_counters
FROM counter_gaps
WHERE gap_size > 1
ORDER BY prev_counter;
```

**Expected Output** (if gaps exist):
```
 counter_before_gap | counter_after_gap | gap_size | missing_counters
--------------------+-------------------+----------+------------------
                 23 |                25 |        2 |                1
                 47 |                50 |        3 |                2
                 89 |                92 |        3 |                2
(3 rows)
```

**Explanation**: Shows where rollbacks or aborted transactions occurred. This is normal and doesn't indicate corruption.

## Verification Method 3: Hash Integrity

### Recompute and Verify Hashes

```sql
-- Verify hash computation is correct (requires extension function)
-- This is a conceptual example - actual implementation would need C function

SELECT
    __tx_lsn,
    description,
    __curr_hash = __prev_hash as hash_invalid,  -- Placeholder
    CASE
        WHEN __curr_hash != __prev_hash THEN 'OK'
        ELSE 'CORRUPTED'
    END as status
FROM verification_test
LIMIT 5;
```

**Note**: Full hash recomputation requires a custom function. The blockchain extension ensures hash integrity during insertion.

### Detect Hash Collisions

```sql
-- Check for duplicate hashes (should be impossible with SHA-256)
SELECT
    __curr_hash,
    COUNT(*) as occurrence_count,
    STRING_AGG(__tx_lsn::TEXT, ', ') as affected_counters
FROM verification_test
GROUP BY __curr_hash
HAVING COUNT(*) > 1;
```

**Expected Output**:
```
(0 rows)
```

**Explanation**: SHA-256 collision probability is astronomically low. If this returns results, there's a critical bug.

### Check for Fork Points

```sql
-- Detect multiple blocks pointing to same previous hash (branching)
WITH hash_usage AS (
    SELECT
        __prev_hash,
        COUNT(*) as usage_count,
        STRING_AGG(__tx_lsn::TEXT, ', ' ORDER BY __tx_lsn) as forked_blocks
    FROM verification_test
    WHERE __prev_hash != '\x0000000000000000000000000000000000000000000000000000000000000000'
    GROUP BY __prev_hash
)
SELECT
    COUNT(*) as total_fork_points,
    STRING_AGG(forked_blocks, ' | ') as fork_details
FROM hash_usage
WHERE usage_count > 1;
```

**Expected Output**:
```
 total_fork_points | fork_details
-------------------+--------------
                 0 |
(1 row)
```

**Explanation**: Fork points indicate race conditions in concurrent inserts. Should be 0 with proper implementation.

## Verification Method 4: Temporal Integrity

### Check Timestamp Ordering

```sql
-- Verify timestamps generally increase (allowing for small clock skew)
WITH timestamp_analysis AS (
    SELECT
        __tx_lsn,
        __tx_timestamp,
        LAG(__tx_timestamp) OVER (ORDER BY __tx_lsn) as prev_timestamp,
        __tx_timestamp - LAG(__tx_timestamp) OVER (ORDER BY __tx_lsn) as time_diff
    FROM verification_test
)
SELECT
    COUNT(*) as total_comparisons,
    COUNT(*) FILTER (WHERE time_diff >= INTERVAL '0') as monotonic_count,
    COUNT(*) FILTER (WHERE time_diff < INTERVAL '0') as backward_count,
    MIN(time_diff) as max_backward_jump,
    MAX(time_diff) as max_forward_jump,
    CASE
        WHEN COUNT(*) FILTER (WHERE time_diff < INTERVAL '-1 second') > 0
            THEN 'WARN: Significant backward time jumps detected'
        WHEN COUNT(*) FILTER (WHERE time_diff < INTERVAL '0') > 0
            THEN 'INFO: Minor timestamp variations (clock skew)'
        ELSE 'PASS: Timestamps monotonically increasing'
    END as status
FROM timestamp_analysis
WHERE prev_timestamp IS NOT NULL;
```

**Expected Output**:
```
 total_comparisons | monotonic_count | backward_count | max_backward_jump | max_forward_jump |                  status
-------------------+-----------------+----------------+-------------------+------------------+------------------------------------------
                99 |              99 |              0 | 00:00:00          | 00:00:00.123456  | PASS: Timestamps monotonically increasing
(1 row)
```

### Detect Large Time Gaps

```sql
-- Find unusual time gaps between consecutive blocks
WITH time_gaps AS (
    SELECT
        __tx_lsn,
        __tx_timestamp,
        __tx_timestamp - LAG(__tx_timestamp) OVER (ORDER BY __tx_lsn) as time_gap,
        description
    FROM verification_test
)
SELECT
    __tx_lsn,
    time_gap,
    description
FROM time_gaps
WHERE time_gap > INTERVAL '1 hour'  -- Adjust threshold as needed
ORDER BY time_gap DESC
LIMIT 10;
```

**Expected Output** (if gaps exist):
```
 __tx_lsn |   time_gap   |    description
----------+--------------+-------------------
       45 | 02:30:15     | Transaction 45
       23 | 01:15:00     | Transaction 23
(2 rows)
```

**Explanation**: Large time gaps might indicate system downtime, but don't affect hash integrity.

## Verification Method 5: Built-in Verification Functions

### Using is_blockchain_table()

```sql
-- Verify table is properly configured as blockchain
SELECT
    'verification_test' as table_name,
    is_blockchain_table('verification_test') as is_blockchain,
    CASE
        WHEN is_blockchain_table('verification_test') THEN 'PASS'
        ELSE 'FAIL: Not a blockchain table'
    END as status;
```

**Expected Output**:
```
    table_name     | is_blockchain | status
-------------------+---------------+--------
 verification_test | t             | PASS
(1 row)
```

### Using describe_blockchain_table()

```sql
-- Get detailed table structure
SELECT
    column_name,
    data_type,
    is_system_column
FROM describe_blockchain_table('verification_test')
ORDER BY is_system_column, ordinal_position;
```

**Expected Output**:
```
   column_name   |      data_type       | is_system_column
-----------------+----------------------+------------------
 transaction_id  | integer              | f
 description     | text                 | f
 amount          | numeric              | f
 timestamp       | timestamp            | f
 __row_id        | uuid                 | t
 __curr_hash     | bytea                | t
 __prev_hash     | bytea                | t
 __tx_type       | character            | t
 __tx_lsn        | bigint               | t
 __tx_origin     | uuid                 | t
 __tx_version    | integer              | t
 __is_latest     | boolean              | t
 __tx_timestamp  | timestamp with time zone | t
(13 rows)
```

## Comprehensive Verification Report

### All-in-One Verification Query

```sql
-- Comprehensive verification report
WITH
genesis_check AS (
    SELECT COUNT(*) as genesis_count
    FROM verification_test
    WHERE __prev_hash = '\x0000000000000000000000000000000000000000000000000000000000000000'
),
orphan_check AS (
    SELECT COUNT(*) as orphan_count
    FROM verification_test v1
    WHERE v1.__prev_hash != '\x0000000000000000000000000000000000000000000000000000000000000000'
      AND NOT EXISTS (
          SELECT 1 FROM verification_test v2 WHERE v2.__curr_hash = v1.__prev_hash
      )
),
counter_check AS (
    SELECT
        COUNT(*) as total_blocks,
        COUNT(DISTINCT __tx_lsn) as unique_counters,
        MIN(__tx_lsn) as first_counter,
        MAX(__tx_lsn) as last_counter
    FROM verification_test
),
fork_check AS (
    SELECT COUNT(*) as fork_count
    FROM (
        SELECT __prev_hash
        FROM verification_test
        WHERE __prev_hash != '\x0000000000000000000000000000000000000000000000000000000000000000'
        GROUP BY __prev_hash
        HAVING COUNT(*) > 1
    ) forks
),
hash_chain AS (
    SELECT
        __tx_lsn,
        __curr_hash,
        __prev_hash,
        1 as depth
    FROM verification_test
    WHERE __prev_hash = '\x0000000000000000000000000000000000000000000000000000000000000000'

    UNION ALL

    SELECT
        v.__tx_lsn,
        v.__curr_hash,
        v.__prev_hash,
        hc.depth + 1
    FROM verification_test v
    INNER JOIN hash_chain hc ON v.__prev_hash = hc.__curr_hash
)
SELECT
    'Total Blocks' as metric,
    cc.total_blocks::TEXT as value,
    CASE WHEN cc.total_blocks > 0 THEN 'PASS' ELSE 'FAIL' END as status
FROM counter_check cc

UNION ALL

SELECT
    'Genesis Blocks',
    gc.genesis_count::TEXT,
    CASE WHEN gc.genesis_count = 1 THEN 'PASS' ELSE 'FAIL' END
FROM genesis_check gc

UNION ALL

SELECT
    'Orphan Blocks',
    oc.orphan_count::TEXT,
    CASE WHEN oc.orphan_count = 0 THEN 'PASS' ELSE 'FAIL' END
FROM orphan_check oc

UNION ALL

SELECT
    'Fork Points',
    fc.fork_count::TEXT,
    CASE WHEN fc.fork_count = 0 THEN 'PASS' ELSE 'FAIL' END
FROM fork_check fc

UNION ALL

SELECT
    'Unique Counters',
    cc.unique_counters::TEXT,
    CASE WHEN cc.total_blocks = cc.unique_counters THEN 'PASS' ELSE 'FAIL' END
FROM counter_check cc

UNION ALL

SELECT
    'Counter Range',
    cc.first_counter || ' to ' || cc.last_counter,
    'INFO'
FROM counter_check cc

UNION ALL

SELECT
    'Reachable Blocks',
    COUNT(*)::TEXT,
    CASE WHEN COUNT(*) = (SELECT total_blocks FROM counter_check) THEN 'PASS' ELSE 'FAIL' END
FROM hash_chain;
```

**Expected Output**:
```
      metric       |    value    | status
-------------------+-------------+--------
 Total Blocks      | 100         | PASS
 Genesis Blocks    | 1           | PASS
 Orphan Blocks     | 0           | PASS
 Fork Points       | 0           | PASS
 Unique Counters   | 100         | PASS
 Counter Range     | 1 to 100    | INFO
 Reachable Blocks  | 100         | PASS
(7 rows)
```

**Time**: ~200ms for 100 blocks

## Performance Testing Verification

### Measure Verification Query Performance

```sql
-- Test verification performance
\timing on

-- Run comprehensive verification
WITH RECURSIVE hash_chain AS (
    SELECT __tx_lsn, __curr_hash, __prev_hash, 1 as depth
    FROM verification_test
    WHERE __prev_hash = '\x0000000000000000000000000000000000000000000000000000000000000000'
    UNION ALL
    SELECT v.__tx_lsn, v.__curr_hash, v.__prev_hash, hc.depth + 1
    FROM verification_test v
    INNER JOIN hash_chain hc ON v.__prev_hash = hc.__curr_hash
)
SELECT COUNT(*) FROM hash_chain;

\timing off
```

**Expected Timing**:
- 100 blocks: ~50-100ms
- 1,000 blocks: ~200-400ms
- 10,000 blocks: ~1-2 seconds
- 100,000 blocks: ~10-20 seconds

## Detecting and Diagnosing Chain Breaks

### Simulate a Break (For Testing Only)

```sql
-- WARNING: This corrupts your blockchain for testing purposes only!
-- DO NOT run on production data

-- Backup first
CREATE TABLE verification_backup AS SELECT * FROM verification_test;

-- Simulate corruption by manually updating a hash (requires superuser)
-- This would normally be prevented by the blockchain access method
-- UPDATE verification_test SET __curr_hash = '\x1111111111111111...' WHERE __tx_lsn = 50;
```

### Diagnose the Break

```sql
-- Find the exact point of chain break
WITH RECURSIVE hash_chain AS (
    SELECT
        __tx_lsn,
        __curr_hash,
        __prev_hash,
        1 as depth,
        ARRAY[__tx_lsn] as chain_path
    FROM verification_test
    WHERE __prev_hash = '\x0000000000000000000000000000000000000000000000000000000000000000'

    UNION ALL

    SELECT
        v.__tx_lsn,
        v.__curr_hash,
        v.__prev_hash,
        hc.depth + 1,
        hc.chain_path || v.__tx_lsn
    FROM verification_test v
    INNER JOIN hash_chain hc ON v.__prev_hash = hc.__curr_hash
    WHERE hc.depth < 1000  -- Prevent infinite recursion
)
SELECT
    MAX(depth) as max_reachable_depth,
    (SELECT COUNT(*) FROM verification_test) as total_blocks,
    (SELECT COUNT(*) FROM verification_test) - MAX(depth) as unreachable_blocks,
    MAX(chain_path) as last_valid_chain
FROM hash_chain;
```

**Output if broken at block 50**:
```
 max_reachable_depth | total_blocks | unreachable_blocks | last_valid_chain
---------------------+--------------+--------------------+------------------
                  49 |          100 |                 51 | {1,2,3,...,49}
(1 row)
```

## Cleanup

```sql
-- Drop test table
DROP TABLE verification_test;

-- Drop backup if created
DROP TABLE IF EXISTS verification_backup;
```

## Key Takeaways

1. **Multiple Verification Layers**: Use genesis checks, hash linkage, counter integrity, and temporal checks together.

2. **Recursive Chain Traversal**: The most comprehensive verification method - if all blocks are reachable from genesis, the chain is intact.

3. **Counter Gaps Are Normal**: Gaps in `__tx_lsn` occur during transaction rollbacks and concurrent operations. They don't indicate corruption if hashes still link correctly.

4. **Zero Fork Points**: The concurrency mechanism should ensure no branching. Fork points indicate implementation issues.

5. **Performance Scales**: Verification time is O(N) for simple checks, O(N log N) for recursive traversal.

6. **Automated Verification**: Create scheduled jobs to run verification reports and alert on failures.

7. **Blockchain Immutability**: Even blockchain tables can't be modified (UPDATE/DELETE blocked), but manual corruption via system tables would break integrity checks.

## Related Examples

- [Concurrent Inserts](concurrent-inserts.md) - Stress testing for verification
- [Query Anchoring](query-anchoring.md) - Verify anchored query results
- [Audit Logging](audit-logging.md) - Real-world verification use case
- [Troubleshooting](troubleshooting.md) - Diagnose verification failures
