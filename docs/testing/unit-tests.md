# Unit Tests

## One-Minute Summary

Unit tests validate individual components of the blockchain extension in isolation:
- **Hash computation**: SHA-256 correctness and determinism
- **Counter management**: Atomic increment and persistence
- **System columns**: Automatic generation and visibility
- **Helper functions**: Utility function behavior

Run unit tests to verify core functionality before integration testing. All tests should complete in under 1 minute.

## Test Components

### 1. Hash Computation Tests

#### Test: SHA-256 Determinism
```sql
-- Create test table
CREATE BLOCKCHAIN TABLE hash_test (data TEXT);

-- Insert same data multiple times
INSERT INTO hash_test (data) VALUES ('test_data_1');
INSERT INTO hash_test (data) VALUES ('test_data_2');
INSERT INTO hash_test (data) VALUES ('test_data_1');  -- Duplicate

-- Verify different data produces different hashes
SELECT
    data,
    __tx_lsn,
    encode(__curr_hash, 'hex') AS hash_hex
FROM hash_test
ORDER BY __tx_lsn;

-- Expected: Rows 1 and 3 have same user data but DIFFERENT hashes
-- (because hash includes counter and previous hash)
```

**Expected Result**:
```
 data        | __tx_lsn | hash_hex
-------------+----------+------------------------------------------
 test_data_1 |        1 | a7f8e9... (different for each row)
 test_data_2 |        2 | 3c2d1b...
 test_data_1 |        3 | 9e4f2a... (different from row 1)
```

**Verification**:
- All hashes are 64 hex characters (32 bytes)
- Same input data produces different hashes due to counter/prev_hash
- All hashes are valid SHA-256 outputs

#### Test: Hash Chain Linkage
```sql
-- Verify each hash correctly references predecessor
WITH hash_chain AS (
    SELECT
        __tx_lsn AS seq,
        __curr_hash AS curr,
        __prev_hash AS prev,
        LAG(__curr_hash) OVER (ORDER BY __tx_lsn) AS expected_prev
    FROM hash_test
)
SELECT
    seq,
    CASE
        WHEN seq = 1 AND prev = '\x0000000000000000000000000000000000000000000000000000000000000000'
            THEN 'PASS: Genesis block'
        WHEN seq > 1 AND prev = expected_prev
            THEN 'PASS: Linked'
        ELSE 'FAIL: Broken link'
    END AS status
FROM hash_chain
ORDER BY seq;
```

**Expected Result**:
```
 seq | status
-----+--------------------
   1 | PASS: Genesis block
   2 | PASS: Linked
   3 | PASS: Linked
```

### 2. Counter Management Tests

#### Test: Counter Atomicity
```sql
-- Create test table
CREATE BLOCKCHAIN TABLE counter_test (id INT);

-- Insert multiple rows
INSERT INTO counter_test (id) VALUES (1), (2), (3), (4), (5);

-- Verify counters are sequential
SELECT
    id,
    __tx_lsn,
    __tx_lsn - LAG(__tx_lsn, 1, 0) OVER (ORDER BY __tx_lsn) AS counter_gap
FROM counter_test
ORDER BY __tx_lsn;
```

**Expected Result**:
```
 id | __tx_lsn | counter_gap
----+----------+-------------
  1 |        1 |           1
  2 |        2 |           1
  3 |        3 |           1
  4 |        4 |           1
  5 |        5 |           1
```

**Verification**:
- `counter_gap` should always be 1
- No duplicate counters
- No skipped counters (in successful transactions)

#### Test: Counter Persistence
```sql
-- Record max counter before restart
CREATE TEMP TABLE counter_checkpoint AS
SELECT MAX(__tx_lsn) AS max_counter FROM counter_test;

-- Display for manual verification
SELECT * FROM counter_checkpoint;

-- Instructions for manual test:
-- 1. Note the max_counter value
-- 2. Restart PostgreSQL: pg_ctl restart -D $PGDATA
-- 3. After restart, run:
INSERT INTO counter_test (id) VALUES (999);
SELECT id, __tx_lsn FROM counter_test WHERE id = 999;

-- 4. Verify: new __tx_lsn should equal (old max_counter + 1)
```

**Expected Result**: Counter continues from previous maximum, proving persistence across restarts.

### 3. System Column Tests

#### Test: System Column Generation
```sql
-- Create minimal table
CREATE BLOCKCHAIN TABLE syscol_test (value INT);

-- Insert data
INSERT INTO syscol_test (value) VALUES (42);

-- Verify all system columns are populated
SELECT
    value,
    __row_id IS NOT NULL AS has_row_id,
    __tx_lsn IS NOT NULL AS has_tx_lsn,
    __curr_hash IS NOT NULL AS has_curr_hash,
    __prev_hash IS NOT NULL AS has_prev_hash,
    __tx_type IS NOT NULL AS has_tx_type,
    __tx_origin IS NOT NULL AS has_tx_origin,
    __tx_version IS NOT NULL AS has_tx_version,
    __is_latest IS NOT NULL AS has_is_latest,
    __tx_timestamp IS NOT NULL AS has_tx_timestamp
FROM syscol_test;
```

**Expected Result**:
```
 value | has_row_id | has_tx_lsn | has_curr_hash | has_prev_hash | has_tx_type | has_tx_origin | has_tx_version | has_is_latest | has_tx_timestamp
-------+------------+------------+---------------+---------------+-------------+---------------+----------------+---------------+------------------
    42 | t          | t          | t             | t             | t           | t             | t              | t             | t
```

#### Test: System Column Hiding
```sql
-- SELECT * should hide system columns
SELECT * FROM syscol_test;

-- Expected: Only user columns (value)
-- Output: value | 42

-- Explicit system column access should work
SELECT value, __row_id, __tx_lsn FROM syscol_test;

-- Expected: All requested columns visible
```

### 4. Helper Function Tests

#### Test: is_blockchain_table()
```sql
-- Create test tables
CREATE BLOCKCHAIN TABLE bc_table (id INT);
CREATE TABLE regular_table (id INT);

-- Test helper function
SELECT
    'bc_table' AS table_name,
    is_blockchain_table('bc_table') AS is_blockchain
UNION ALL
SELECT
    'regular_table',
    is_blockchain_table('regular_table')
UNION ALL
SELECT
    'nonexistent_table',
    is_blockchain_table('nonexistent_table');
```

**Expected Result**:
```
 table_name         | is_blockchain
--------------------+---------------
 bc_table           | t
 regular_table      | f
 nonexistent_table  | f
```

#### Test: describe_blockchain_table()
```sql
-- Test table description
SELECT * FROM describe_blockchain_table('bc_table');
```

**Expected Result**:
```
 column_name    | data_type | is_system_column
----------------+-----------+------------------
 id             | integer   | f
 __row_id       | uuid      | t
 __tx_lsn       | bigint    | t
 __curr_hash    | bytea     | t
 __prev_hash    | bytea     | t
 __tx_type      | text      | t
 __tx_origin    | text      | t
 __tx_version   | integer   | t
 __is_latest    | boolean   | t
 __tx_timestamp | timestamp | t
```

### 5. Query Anchoring Unit Tests

#### Test: create_anchor_table()
```sql
-- Create anchor table
SELECT create_anchor_table('test_anchors');

-- Verify table exists and has correct structure
SELECT column_name, data_type
FROM information_schema.columns
WHERE table_name = 'test_anchors'
ORDER BY ordinal_position;
```

**Expected Result**:
```
 column_name  | data_type
--------------+-----------
 query_id     | text
 query_text   | text
 result_hash  | bytea
 anchor_time  | timestamp with time zone
 user_id      | text
 notes        | text
 __row_id     | uuid
 __tx_lsn     | bigint
 ... (other system columns)
```

#### Test: anchor_query_result()
```sql
-- Create source data
CREATE TABLE source_data (id INT, value TEXT);
INSERT INTO source_data VALUES (1, 'alpha'), (2, 'beta'), (3, 'gamma');

-- Anchor query result
SELECT anchor_query_result(
    'test_anchors',
    'test_query_1',
    'SELECT * FROM source_data ORDER BY id',
    'Test query anchoring'
);

-- Verify anchor was stored
SELECT query_id, length(result_hash) AS hash_length, notes
FROM test_anchors
WHERE query_id = 'test_query_1';
```

**Expected Result**:
```
 query_id      | hash_length | notes
---------------+-------------+------------------------
 test_query_1  |          32 | Test query anchoring
```

#### Test: verify_query_anchor()
```sql
-- Verify the anchor (should pass)
SELECT verify_query_anchor('test_anchors', 'test_query_1') AS verification_result;

-- Expected: t (true)

-- Modify source data
UPDATE source_data SET value = 'MODIFIED' WHERE id = 1;

-- Verify again (should fail if source is blockchain, otherwise shows data changed)
-- Note: This demonstrates that verification detects data changes
```

## Running All Unit Tests

### Automated Test Suite
```sql
-- unit_test_suite.sql
\set ON_ERROR_STOP on
\echo '=== Starting Unit Test Suite ==='

-- Test 1: Hash Computation
\echo 'Test 1: Hash Computation'
CREATE BLOCKCHAIN TABLE unit_hash_test (data TEXT);
INSERT INTO unit_hash_test VALUES ('A'), ('B'), ('A');
SELECT COUNT(DISTINCT __curr_hash) = 3 AS test_1_pass FROM unit_hash_test;

-- Test 2: Counter Atomicity
\echo 'Test 2: Counter Atomicity'
CREATE BLOCKCHAIN TABLE unit_counter_test (val INT);
INSERT INTO unit_counter_test SELECT generate_series(1, 100);
SELECT
    (MAX(__tx_lsn) - MIN(__tx_lsn) + 1) = COUNT(*) AS test_2_pass
FROM unit_counter_test;

-- Test 3: System Columns
\echo 'Test 3: System Column Generation'
CREATE BLOCKCHAIN TABLE unit_syscol_test (x INT);
INSERT INTO unit_syscol_test VALUES (1);
SELECT
    (__row_id IS NOT NULL AND __tx_lsn IS NOT NULL AND __curr_hash IS NOT NULL) AS test_3_pass
FROM unit_syscol_test;

-- Test 4: Helper Functions
\echo 'Test 4: Helper Functions'
SELECT
    is_blockchain_table('unit_syscol_test') = true AS test_4_pass;

-- Test 5: Query Anchoring
\echo 'Test 5: Query Anchoring'
SELECT create_anchor_table('unit_test_anchors');
CREATE TABLE unit_anchor_source (id INT);
INSERT INTO unit_anchor_source VALUES (1), (2), (3);
SELECT anchor_query_result(
    'unit_test_anchors',
    'unit_test_q1',
    'SELECT * FROM unit_anchor_source ORDER BY id',
    NULL
);
SELECT verify_query_anchor('unit_test_anchors', 'unit_test_q1') AS test_5_pass;

\echo '=== Unit Test Suite Complete ==='

-- Cleanup
DROP TABLE unit_hash_test, unit_counter_test, unit_syscol_test;
DROP TABLE unit_test_anchors, unit_anchor_source;
```

### Running the Suite
```bash
psql -d testdb -f unit_test_suite.sql

# Expected output:
# Test 1: Hash Computation
#  test_1_pass
# -------------
#  t
# Test 2: Counter Atomicity
#  test_2_pass
# -------------
#  t
# (etc.)
```

## Test Results Interpretation

### Pass Criteria
- All boolean tests return `t` (true)
- No PostgreSQL errors (except expected immutability violations)
- Hash lengths are exactly 32 bytes
- Counters are sequential and unique
- System columns are auto-populated

### Failure Investigation

#### Hash Test Fails
```sql
-- Debug hash computation
SELECT
    __tx_lsn,
    length(__curr_hash) AS hash_len,
    encode(__curr_hash, 'hex') AS hash_hex
FROM problematic_table
ORDER BY __tx_lsn;

-- Check: hash_len should always be 32
-- Check: hash_hex should be 64 hex characters
```

#### Counter Test Fails
```sql
-- Find counter gaps or duplicates
WITH counter_analysis AS (
    SELECT
        __tx_lsn,
        __tx_lsn - LAG(__tx_lsn) OVER (ORDER BY __tx_lsn) AS gap,
        COUNT(*) OVER (PARTITION BY __tx_lsn) AS dup_count
    FROM problematic_table
)
SELECT * FROM counter_analysis WHERE gap != 1 OR dup_count > 1;
```

## Troubleshooting

### Common Issues

| Symptom | Cause | Solution |
|---------|-------|----------|
| Hash length != 32 | Incorrect SHA-256 implementation | Check `blockchain_hash.c` |
| Duplicate counters | Race condition in counter allocation | Check shared memory locks |
| NULL system columns | AM handler not invoked | Verify table AM is `blockchainam` |
| Helper function missing | Extension not loaded | `CREATE EXTENSION blockchain_anchor;` |

### Debugging Commands

```sql
-- Check table access method
SELECT relname, am.amname
FROM pg_class rel
JOIN pg_am am ON rel.relam = am.oid
WHERE relname = 'your_table';
-- Expected amname: blockchainam

-- Verify system column definitions
SELECT attname, atttypid::regtype
FROM pg_attribute
WHERE attrelid = 'your_blockchain_table'::regclass
  AND attname LIKE '__%'
ORDER BY attnum;

-- Check shared memory usage
SELECT * FROM pg_shmem_allocations
WHERE name LIKE '%blockchain%';
```

## Best Practices

1. **Isolation**: Run each test in a separate transaction or database
2. **Cleanup**: Drop test tables after verification
3. **Determinism**: Use fixed data sets, not random values
4. **Assertions**: Verify expected vs actual results explicitly
5. **Documentation**: Comment complex test logic

## Next Steps

After unit tests pass:
1. Proceed to [Integration Tests](integration-tests.md)
2. Run [Concurrency Tests](concurrency-tests.md) to verify multi-client behavior
3. Execute [Performance Benchmarks](performance.md) to establish baselines

## References

- [Hash Implementation](../../src/backend/access/blockchain/blockchain_hash.c)
- [Counter Implementation](../../src/backend/access/blockchain/blockchain_counter.c)
- [Query Anchoring](../../src/backend/access/blockchain/blockchain_anchor.c)
