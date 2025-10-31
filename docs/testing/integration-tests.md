# Integration Tests

## One-Minute Summary

Integration tests verify end-to-end workflows of the blockchain extension:
- **Table lifecycle**: Creation, population, querying, and restrictions
- **Immutability enforcement**: UPDATE/DELETE/TRUNCATE/ALTER blocking
- **Hash chain integrity**: Multi-row insertion with correct linking
- **Query anchoring**: Full anchor-verify-replay workflow
- **Cross-feature interaction**: Counter + hash + system columns working together

Run integration tests after unit tests pass. Duration: 2-5 minutes.

## Test Workflow

```
┌──────────────────┐
│ Create BC Table  │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ Insert Data      │──► Verify: Hashes linked, counters sequential
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ Test Restrictions│──► Verify: UPDATE/DELETE/ALTER fail
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ Query Anchoring  │──► Verify: Anchor + verification succeed
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ Hash Chain Check │──► Verify: No breaks, single genesis
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ Cleanup          │
└──────────────────┘
```

## Comprehensive Integration Test

### Full Test Script

Create file: `integration_test.sql`

```sql
-- ============================================================================
-- PostgreSQL Blockchain Extension - Integration Test Suite
-- ============================================================================

\set ON_ERROR_STOP off  -- Continue on errors (we expect some to fail)
\echo '=== Integration Test Suite Started ==='

-- Cleanup
DROP TABLE IF EXISTS integration_bc_test CASCADE;
DROP TABLE IF EXISTS integration_anchors CASCADE;
DROP TABLE IF EXISTS integration_source CASCADE;

-- ============================================================================
-- Test 1: Table Creation and Basic Operations
-- ============================================================================
\echo ''
\echo '=== Test 1: Table Creation ==='

CREATE BLOCKCHAIN TABLE integration_bc_test (
    user_id INTEGER NOT NULL,
    username VARCHAR(50) NOT NULL,
    email VARCHAR(100),
    action VARCHAR(200),
    ip_address INET,
    amount NUMERIC(10,2),
    metadata JSONB,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

\echo 'Result: Blockchain table created'

-- Verify it's recognized as blockchain table
SELECT
    'Table Type Check' AS test,
    CASE
        WHEN is_blockchain_table('integration_bc_test') THEN 'PASS'
        ELSE 'FAIL'
    END AS result;

-- ============================================================================
-- Test 2: Data Insertion and System Column Generation
-- ============================================================================
\echo ''
\echo '=== Test 2: Data Insertion ==='

-- Insert test data
INSERT INTO integration_bc_test (user_id, username, email, action, ip_address, amount, metadata)
VALUES
    (1, 'alice', 'alice@example.com', 'login', '192.168.1.100', 0.00, '{"device": "mobile"}'),
    (2, 'bob', 'bob@example.com', 'create_account', '192.168.1.101', 100.00, '{"referral": "alice"}'),
    (3, 'charlie', 'charlie@example.com', 'transfer', '192.168.1.102', 50.00, '{"to": "alice"}'),
    (4, 'diana', 'diana@example.com', 'withdrawal', '192.168.1.103', 25.00, '{"bank": "Chase"}'),
    (5, 'eve', 'eve@example.com', 'deposit', '192.168.1.104', 200.00, '{"method": "card"}');

-- Verify system columns are populated
SELECT
    'System Column Population' AS test,
    CASE
        WHEN COUNT(*) = 5
         AND COUNT(__row_id) = 5
         AND COUNT(__tx_lsn) = 5
         AND COUNT(__curr_hash) = 5
         AND COUNT(__prev_hash) = 5
         AND COUNT(__tx_timestamp) = 5
        THEN 'PASS'
        ELSE 'FAIL'
    END AS result
FROM integration_bc_test;

-- ============================================================================
-- Test 3: Counter Sequence Verification
-- ============================================================================
\echo ''
\echo '=== Test 3: Counter Sequence ==='

SELECT
    'Counter Uniqueness' AS test,
    CASE
        WHEN COUNT(DISTINCT __tx_lsn) = COUNT(*) THEN 'PASS'
        ELSE 'FAIL'
    END AS result
FROM integration_bc_test;

SELECT
    'Counter Monotonicity' AS test,
    CASE
        WHEN MIN(__tx_lsn) = 1
         AND MAX(__tx_lsn) = 5
         AND MAX(__tx_lsn) - MIN(__tx_lsn) + 1 = COUNT(*)
        THEN 'PASS'
        ELSE 'FAIL'
    END AS result
FROM integration_bc_test;

-- ============================================================================
-- Test 4: Hash Chain Integrity
-- ============================================================================
\echo ''
\echo '=== Test 4: Hash Chain Integrity ==='

-- Check genesis block
SELECT
    'Genesis Block' AS test,
    CASE
        WHEN __tx_lsn = 1
         AND __prev_hash = '\x0000000000000000000000000000000000000000000000000000000000000000'
        THEN 'PASS'
        ELSE 'FAIL'
    END AS result
FROM integration_bc_test
WHERE __tx_lsn = 1;

-- Check hash chain linkage
WITH hash_chain AS (
    SELECT
        __tx_lsn,
        __curr_hash,
        __prev_hash,
        LAG(__curr_hash) OVER (ORDER BY __tx_lsn) AS expected_prev_hash
    FROM integration_bc_test
)
SELECT
    'Hash Chain Linkage' AS test,
    CASE
        WHEN COUNT(*) FILTER (WHERE __tx_lsn > 1 AND __prev_hash != expected_prev_hash) = 0
        THEN 'PASS'
        ELSE 'FAIL'
    END AS result
FROM hash_chain;

-- Verify all hashes are unique
SELECT
    'Hash Uniqueness' AS test,
    CASE
        WHEN COUNT(DISTINCT __curr_hash) = COUNT(*) THEN 'PASS'
        ELSE 'FAIL'
    END AS result
FROM integration_bc_test;

-- Verify hash length (32 bytes for SHA-256)
SELECT
    'Hash Length' AS test,
    CASE
        WHEN COUNT(*) FILTER (WHERE length(__curr_hash) != 32) = 0
        THEN 'PASS'
        ELSE 'FAIL'
    END AS result
FROM integration_bc_test;

-- ============================================================================
-- Test 5: Immutability Enforcement - UPDATE
-- ============================================================================
\echo ''
\echo '=== Test 5: UPDATE Restriction ==='

-- This should FAIL (which is expected)
UPDATE integration_bc_test SET email = 'new@example.com' WHERE user_id = 1;

-- Verify data unchanged
SELECT
    'UPDATE Blocked' AS test,
    CASE
        WHEN email = 'alice@example.com' THEN 'PASS'
        ELSE 'FAIL'
    END AS result
FROM integration_bc_test
WHERE user_id = 1;

-- ============================================================================
-- Test 6: Immutability Enforcement - DELETE
-- ============================================================================
\echo ''
\echo '=== Test 6: DELETE Restriction ==='

-- This should FAIL (which is expected)
DELETE FROM integration_bc_test WHERE user_id = 5;

-- Verify row still exists
SELECT
    'DELETE Blocked' AS test,
    CASE
        WHEN COUNT(*) = 1 THEN 'PASS'
        ELSE 'FAIL'
    END AS result
FROM integration_bc_test
WHERE user_id = 5;

-- ============================================================================
-- Test 7: DDL Restrictions - ALTER TABLE
-- ============================================================================
\echo ''
\echo '=== Test 7: ALTER TABLE Restriction ==='

-- These should all FAIL (which is expected)
ALTER TABLE integration_bc_test ADD COLUMN new_col TEXT;
ALTER TABLE integration_bc_test DROP COLUMN metadata;
ALTER TABLE integration_bc_test ALTER COLUMN username TYPE TEXT;

-- Verify schema unchanged
SELECT
    'ALTER TABLE Blocked' AS test,
    CASE
        WHEN COUNT(*) FILTER (WHERE column_name = 'username' AND data_type = 'character varying') = 1
         AND COUNT(*) FILTER (WHERE column_name = 'new_col') = 0
         AND COUNT(*) FILTER (WHERE column_name = 'metadata') = 1
        THEN 'PASS'
        ELSE 'FAIL'
    END AS result
FROM information_schema.columns
WHERE table_name = 'integration_bc_test'
  AND column_name NOT LIKE '__%';

-- ============================================================================
-- Test 8: DDL Restrictions - TRUNCATE
-- ============================================================================
\echo ''
\echo '=== Test 8: TRUNCATE Restriction ==='

-- This should FAIL (which is expected)
TRUNCATE integration_bc_test;

-- Verify data still exists
SELECT
    'TRUNCATE Blocked' AS test,
    CASE
        WHEN COUNT(*) = 5 THEN 'PASS'
        ELSE 'FAIL'
    END AS result
FROM integration_bc_test;

-- ============================================================================
-- Test 9: System Column Hiding
-- ============================================================================
\echo ''
\echo '=== Test 9: System Column Hiding ==='

-- Create temp view to capture SELECT * output
CREATE TEMP VIEW star_select AS SELECT * FROM integration_bc_test;

-- Count columns from SELECT *
SELECT
    'SELECT * Hides System Columns' AS test,
    CASE
        -- Should show only 8 user columns, not 9 system columns
        WHEN (SELECT COUNT(*) FROM information_schema.columns WHERE table_name = 'star_select') = 8
        THEN 'PASS'
        ELSE 'FAIL'
    END AS result;

DROP VIEW star_select;

-- Verify explicit system column access works
SELECT
    'Explicit System Column Access' AS test,
    CASE
        WHEN __row_id IS NOT NULL AND __tx_lsn IS NOT NULL
        THEN 'PASS'
        ELSE 'FAIL'
    END AS result
FROM integration_bc_test
LIMIT 1;

-- ============================================================================
-- Test 10: Bulk Insert
-- ============================================================================
\echo ''
\echo '=== Test 10: Bulk Insert ==='

-- Insert 100 more rows
INSERT INTO integration_bc_test (user_id, username, email, action, ip_address, amount)
SELECT
    1000 + gs,
    'user_' || gs,
    'user_' || gs || '@example.com',
    CASE (gs % 4)
        WHEN 0 THEN 'login'
        WHEN 1 THEN 'logout'
        WHEN 2 THEN 'transfer'
        ELSE 'deposit'
    END,
    ('192.168.' || (gs % 255) || '.' || (gs % 255))::inet,
    (gs * 10.5)::numeric(10,2)
FROM generate_series(1, 100) gs;

-- Verify bulk insert integrity
SELECT
    'Bulk Insert Counter Continuity' AS test,
    CASE
        WHEN MAX(__tx_lsn) = 105  -- 5 + 100
         AND MIN(__tx_lsn) = 1
         AND COUNT(DISTINCT __tx_lsn) = 105
        THEN 'PASS'
        ELSE 'FAIL'
    END AS result
FROM integration_bc_test;

-- Verify bulk insert hash chain
WITH hash_check AS (
    SELECT
        __tx_lsn,
        __prev_hash,
        LAG(__curr_hash) OVER (ORDER BY __tx_lsn) AS expected_prev
    FROM integration_bc_test
)
SELECT
    'Bulk Insert Hash Chain' AS test,
    CASE
        WHEN COUNT(*) FILTER (WHERE __tx_lsn > 1 AND __prev_hash != expected_prev) = 0
        THEN 'PASS'
        ELSE 'FAIL'
    END AS result
FROM hash_check;

-- ============================================================================
-- Test 11: Query Anchoring Integration
-- ============================================================================
\echo ''
\echo '=== Test 11: Query Anchoring ==='

-- Create anchor table
SELECT create_anchor_table('integration_anchors');

-- Create source data for anchoring
CREATE TABLE integration_source (
    category VARCHAR(20),
    amount NUMERIC
);

INSERT INTO integration_source VALUES
    ('sales', 1000.00),
    ('expenses', 500.00),
    ('sales', 1500.00),
    ('expenses', 300.00);

-- Anchor a query result
SELECT anchor_query_result(
    'integration_anchors',
    'summary_q1_2025',
    'SELECT category, SUM(amount) as total FROM integration_source GROUP BY category ORDER BY category',
    'Q1 2025 financial summary'
) AS anchor_hash \gset

-- Verify anchor was stored
SELECT
    'Query Anchor Storage' AS test,
    CASE
        WHEN COUNT(*) = 1
         AND query_id = 'summary_q1_2025'
         AND length(result_hash) = 32
        THEN 'PASS'
        ELSE 'FAIL'
    END AS result
FROM integration_anchors
WHERE query_id = 'summary_q1_2025';

-- Verify the anchor
SELECT
    'Query Anchor Verification' AS test,
    CASE
        WHEN verify_query_anchor('integration_anchors', 'summary_q1_2025')
        THEN 'PASS'
        ELSE 'FAIL'
    END AS result;

-- Test verification failure detection (modify source data)
UPDATE integration_source SET amount = 9999 WHERE category = 'sales' LIMIT 1;

-- Re-verify (should fail now due to data change)
SELECT
    'Tamper Detection' AS test,
    CASE
        WHEN NOT verify_query_anchor('integration_anchors', 'summary_q1_2025')
        THEN 'PASS'
        ELSE 'FAIL'
    END AS result;

-- ============================================================================
-- Test 12: Multi-Table Hash Chain Isolation
-- ============================================================================
\echo ''
\echo '=== Test 12: Multi-Table Isolation ==='

-- Create second blockchain table
CREATE BLOCKCHAIN TABLE integration_bc_test_2 (val INT);
INSERT INTO integration_bc_test_2 VALUES (1), (2), (3);

-- Verify both tables have independent counters starting at 1
SELECT
    'Independent Counter Sequences' AS test,
    CASE
        WHEN t1.min_lsn = 1 AND t2.min_lsn = 1
         AND t1.max_lsn = 105 AND t2.max_lsn = 3
        THEN 'PASS'
        ELSE 'FAIL'
    END AS result
FROM
    (SELECT MIN(__tx_lsn) AS min_lsn, MAX(__tx_lsn) AS max_lsn FROM integration_bc_test) t1,
    (SELECT MIN(__tx_lsn) AS min_lsn, MAX(__tx_lsn) AS max_lsn FROM integration_bc_test_2) t2;

-- ============================================================================
-- Test Summary
-- ============================================================================
\echo ''
\echo '=== Integration Test Summary ==='

-- Count rows
SELECT
    'Total Rows' AS metric,
    COUNT(*)::TEXT AS value
FROM integration_bc_test
UNION ALL
SELECT
    'Unique Counters',
    COUNT(DISTINCT __tx_lsn)::TEXT
FROM integration_bc_test
UNION ALL
SELECT
    'Unique Hashes',
    COUNT(DISTINCT __curr_hash)::TEXT
FROM integration_bc_test
UNION ALL
SELECT
    'Genesis Blocks',
    COUNT(*)::TEXT
FROM integration_bc_test
WHERE __prev_hash = '\x0000000000000000000000000000000000000000000000000000000000000000';

-- Final hash chain verification
WITH full_chain_check AS (
    SELECT
        __tx_lsn,
        __prev_hash,
        LAG(__curr_hash) OVER (ORDER BY __tx_lsn) AS expected_prev
    FROM integration_bc_test
)
SELECT
    'Hash Chain Status' AS metric,
    CASE
        WHEN COUNT(*) FILTER (WHERE __tx_lsn > 1 AND __prev_hash != expected_prev) = 0
        THEN 'PERFECT - 0 breaks'
        ELSE 'BROKEN - ' || COUNT(*) FILTER (WHERE __tx_lsn > 1 AND __prev_hash != expected_prev) || ' breaks'
    END AS value
FROM full_chain_check;

\echo ''
\echo '=== Integration Test Suite Complete ==='

-- Cleanup (optional - comment out to inspect results)
-- DROP TABLE integration_bc_test CASCADE;
-- DROP TABLE integration_bc_test_2 CASCADE;
-- DROP TABLE integration_anchors CASCADE;
-- DROP TABLE integration_source CASCADE;
```

## Running Integration Tests

### Command Line
```bash
psql -d testdb -f integration_test.sql > integration_test_results.txt 2>&1

# Review results
less integration_test_results.txt

# Check for failures
grep "FAIL" integration_test_results.txt
```

### Expected Output
```
=== Integration Test Suite Started ===

=== Test 1: Table Creation ===
Result: Blockchain table created
 test             | result
------------------+--------
 Table Type Check | PASS

=== Test 2: Data Insertion ===
 test                       | result
----------------------------+--------
 System Column Population   | PASS

=== Test 3: Counter Sequence ===
 test                 | result
----------------------+--------
 Counter Uniqueness   | PASS
 Counter Monotonicity | PASS

=== Test 4: Hash Chain Integrity ===
 test               | result
--------------------+--------
 Genesis Block      | PASS
 Hash Chain Linkage | PASS
 Hash Uniqueness    | PASS
 Hash Length        | PASS

=== Test 5: UPDATE Restriction ===
ERROR:  UPDATE operation not allowed on blockchain table
 test          | result
---------------+--------
 UPDATE Blocked | PASS

... (continued)

=== Integration Test Summary ===
 metric             | value
--------------------+------------------
 Total Rows         | 105
 Unique Counters    | 105
 Unique Hashes      | 105
 Genesis Blocks     | 1
 Hash Chain Status  | PERFECT - 0 breaks

=== Integration Test Suite Complete ===
```

## Troubleshooting

### Test Failures

#### Hash Chain Broken
```sql
-- Find specific break points
WITH chain_analysis AS (
    SELECT
        __tx_lsn,
        encode(__curr_hash, 'hex') AS curr,
        encode(__prev_hash, 'hex') AS prev,
        encode(LAG(__curr_hash) OVER (ORDER BY __tx_lsn), 'hex') AS expected_prev
    FROM integration_bc_test
)
SELECT * FROM chain_analysis
WHERE __tx_lsn > 1 AND prev != expected_prev;
```

#### Counter Sequence Issues
```sql
-- Find gaps or duplicates
WITH counter_analysis AS (
    SELECT
        __tx_lsn,
        __tx_lsn - LAG(__tx_lsn, 1, 0) OVER (ORDER BY __tx_lsn) AS gap,
        COUNT(*) OVER (PARTITION BY __tx_lsn) AS duplicates
    FROM integration_bc_test
)
SELECT * FROM counter_analysis
WHERE gap > 1 OR duplicates > 1;
```

#### Immutability Not Enforced
```sql
-- Check table access method
SELECT
    relname,
    am.amname
FROM pg_class rel
JOIN pg_am am ON rel.relam = am.oid
WHERE relname = 'integration_bc_test';

-- Should show: amname = 'blockchainam'
```

## Best Practices

1. **Run in Sequence**: Don't parallelize integration tests (they may interfere)
2. **Clean Environment**: Start with fresh database or dropped tables
3. **Capture Output**: Redirect to file for later analysis
4. **Expected Errors**: Some tests intentionally trigger errors (UPDATE/DELETE/etc)
5. **Verify Cleanup**: Ensure test tables are dropped after completion

## Performance Expectations

| Test Section | Expected Duration |
|--------------|-------------------|
| Table Creation | < 1 second |
| Initial Inserts (5 rows) | < 1 second |
| Hash Chain Checks | < 1 second |
| Immutability Tests | < 1 second |
| Bulk Insert (100 rows) | 1-3 seconds |
| Query Anchoring | 1-2 seconds |
| Total | 2-5 minutes |

## Next Steps

After integration tests pass:
1. Run [Concurrency Tests](concurrency-tests.md) to verify multi-client behavior
2. Execute [Performance Benchmarks](performance.md) for throughput analysis
3. Review [Security Documentation](../security/index.md) for production deployment

## References

- [Blockchain Extension Architecture](../architecture/index.md)
- [Immutability Enforcement](../security/immutability.md)
- [Query Anchoring Guide](../api/query-anchoring.md)
