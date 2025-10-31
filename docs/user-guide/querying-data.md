# Querying Data from Blockchain Tables

Master the art of querying blockchain tables, from basic SELECTs to complex joins and system column access.

## One-Minute Summary

!!! abstract "Key Takeaways"
    - **Standard SQL** works perfectly - SELECT, WHERE, JOIN, GROUP BY, etc.
    - **System columns** are hidden from `SELECT *` but accessible by explicit naming
    - **Indexes** work normally for query performance optimization
    - **Joins** with regular tables and other blockchain tables work seamlessly

---

## Basic SELECT Queries

Blockchain tables work exactly like regular PostgreSQL tables for SELECT operations.

### Simple SELECT

```sql
-- Select all user-defined columns
SELECT * FROM audit_log;
```

**Output:**
```
 user_id |     action      | resource_id |   ip_address   | details
---------+-----------------+-------------+----------------+---------
    1001 | login           |             | 192.168.1.100  | {...}
    1001 | view_document   |        5432 | 192.168.1.100  | {...}
    1002 | logout          |             | 10.0.0.25      | {...}
```

!!! info "System Columns Hidden by Default"
    Notice that `__tx_lsn`, `__curr_hash`, and other system columns are NOT shown in `SELECT *` queries. This keeps your regular queries clean.

### SELECT with Specific Columns

```sql
-- Select specific user-defined columns
SELECT user_id, action, ip_address
FROM audit_log;
```

---

## Accessing System Columns

To view blockchain metadata, explicitly name the system columns.

### All System Columns

```sql
SELECT
    -- User columns
    user_id,
    action,

    -- System columns (explicitly named)
    __row_id,
    __tx_lsn,
    __tx_timestamp,
    encode(__curr_hash, 'hex') as current_hash,
    encode(__prev_hash, 'hex') as previous_hash,
    __tx_type,
    __tx_version,
    __is_latest
FROM audit_log
ORDER BY __tx_lsn;
```

**Output:**
```
 user_id |    action    |               __row_id               | __tx_lsn |      __tx_timestamp     |     current_hash     |    previous_hash     | __tx_type | __tx_version | __is_latest
---------+--------------+--------------------------------------+----------+-------------------------+----------------------+----------------------+-----------+--------------+-------------
    1001 | login        | 550e8400-e29b-41d4-a716-446655440000 |        1 | 2025-10-31 10:00:00+00  | 3abb974851d8201b...  |                      | INSERT    |            1 | t
    1001 | view_doc     | 6ba7b810-9dad-11d1-80b4-00c04fd430c8 |        2 | 2025-10-31 10:05:00+00  | a1b2c3d4e5f67890...  | 3abb974851d8201b...  | INSERT    |            1 | t
    1002 | logout       | 7c9e6679-7425-40de-944b-e07fc1f90ae7 |        3 | 2025-10-31 10:10:00+00  | f1e2d3c4b5a69870...  | a1b2c3d4e5f67890...  | INSERT    |            1 | t
```

### Common System Column Patterns

=== "Hash Chain View"

    ```sql
    -- View hash chain linkage
    SELECT
        __tx_lsn as sequence,
        encode(__curr_hash, 'hex') as current,
        encode(__prev_hash, 'hex') as previous,
        CASE
            WHEN __prev_hash IS NULL THEN 'GENESIS'
            ELSE 'LINKED'
        END as chain_status
    FROM audit_log
    ORDER BY __tx_lsn;
    ```

=== "Temporal Analysis"

    ```sql
    -- Analyze insertion timing
    SELECT
        __tx_lsn,
        action,
        __tx_timestamp,
        __tx_timestamp - LAG(__tx_timestamp) OVER (ORDER BY __tx_lsn) as time_since_previous
    FROM audit_log
    ORDER BY __tx_lsn;
    ```

=== "Row Identification"

    ```sql
    -- Use row_id for unique identification
    SELECT
        __row_id as unique_identifier,
        user_id,
        action,
        __tx_timestamp as recorded_at
    FROM audit_log
    WHERE user_id = 1001;
    ```

---

## Filtering with WHERE Clauses

Standard PostgreSQL WHERE clauses work on both user and system columns.

### Filter by User Columns

```sql
-- Find all login actions
SELECT * FROM audit_log
WHERE action = 'login';

-- Find actions for specific user
SELECT * FROM audit_log
WHERE user_id = 1001;

-- Complex conditions
SELECT * FROM audit_log
WHERE action IN ('login', 'logout')
  AND ip_address << '192.168.1.0/24'::inet
  AND details->>'access_level' = 'admin';
```

### Filter by System Columns

```sql
-- Find records inserted in last 24 hours
SELECT user_id, action, __tx_timestamp
FROM audit_log
WHERE __tx_timestamp >= NOW() - INTERVAL '24 hours'
ORDER BY __tx_timestamp DESC;

-- Find records by sequence range
SELECT * FROM audit_log
WHERE __tx_lsn BETWEEN 1000 AND 2000
ORDER BY __tx_lsn;

-- Find genesis block
SELECT * FROM audit_log
WHERE __prev_hash IS NULL;
```

### Performance Optimization with Indexes

```sql
-- Create index on frequently queried columns
CREATE INDEX idx_audit_user ON audit_log(user_id);
CREATE INDEX idx_audit_timestamp ON audit_log(__tx_timestamp);
CREATE INDEX idx_audit_action ON audit_log(action);

-- Now queries using these columns are much faster
SELECT * FROM audit_log
WHERE user_id = 1001  -- Uses idx_audit_user
ORDER BY __tx_timestamp DESC;  -- Uses idx_audit_timestamp
```

!!! tip "Indexes on Blockchain Tables"
    Standard PostgreSQL indexes (B-tree, Hash, GIN, GiST) work normally on blockchain tables. Create indexes on frequently queried columns for better performance.

---

## Sorting and Ordering

Use ORDER BY with any column, including system columns.

### Common Ordering Patterns

=== "Chronological Order"

    ```sql
    -- Order by insertion sequence (fastest)
    SELECT * FROM audit_log
    ORDER BY __tx_lsn;

    -- Order by timestamp
    SELECT * FROM audit_log
    ORDER BY __tx_timestamp DESC;
    ```

=== "Business Logic Order"

    ```sql
    -- Order by user-defined columns
    SELECT * FROM transactions
    ORDER BY account_id, transaction_date;

    -- Complex ordering
    SELECT * FROM audit_log
    ORDER BY
        user_id,
        __tx_timestamp DESC,
        action;
    ```

=== "Hash Chain Traversal"

    ```sql
    -- Walk the chain backwards from latest
    WITH RECURSIVE chain AS (
        -- Start with latest block
        SELECT * FROM audit_log
        WHERE __tx_lsn = (SELECT MAX(__tx_lsn) FROM audit_log)

        UNION ALL

        -- Follow prev_hash backwards
        SELECT a.*
        FROM audit_log a
        JOIN chain c ON a.__curr_hash = c.__prev_hash
    )
    SELECT * FROM chain;
    ```

---

## Aggregations and GROUP BY

Standard SQL aggregations work perfectly on blockchain tables.

### Basic Aggregations

```sql
-- Count total records
SELECT COUNT(*) as total_records
FROM audit_log;

-- Count by action type
SELECT
    action,
    COUNT(*) as count
FROM audit_log
GROUP BY action
ORDER BY count DESC;

-- Summary statistics
SELECT
    COUNT(*) as total_rows,
    COUNT(DISTINCT user_id) as unique_users,
    MIN(__tx_timestamp) as first_record,
    MAX(__tx_timestamp) as last_record
FROM audit_log;
```

### Time-Based Aggregations

```sql
-- Daily activity summary
SELECT
    DATE(__tx_timestamp) as activity_date,
    COUNT(*) as total_actions,
    COUNT(DISTINCT user_id) as unique_users,
    array_agg(DISTINCT action) as action_types
FROM audit_log
GROUP BY DATE(__tx_timestamp)
ORDER BY activity_date DESC;

-- Hourly breakdown
SELECT
    DATE_TRUNC('hour', __tx_timestamp) as hour,
    action,
    COUNT(*) as count
FROM audit_log
WHERE __tx_timestamp >= NOW() - INTERVAL '7 days'
GROUP BY DATE_TRUNC('hour', __tx_timestamp), action
ORDER BY hour DESC, count DESC;
```

### Advanced Aggregations

=== "Window Functions"

    ```sql
    -- Running totals and rankings
    SELECT
        user_id,
        action,
        __tx_timestamp,
        COUNT(*) OVER (PARTITION BY user_id ORDER BY __tx_lsn) as user_action_count,
        ROW_NUMBER() OVER (PARTITION BY action ORDER BY __tx_timestamp) as action_sequence
    FROM audit_log;
    ```

=== "HAVING Clause"

    ```sql
    -- Find users with >10 actions
    SELECT
        user_id,
        COUNT(*) as action_count,
        array_agg(action) as actions
    FROM audit_log
    GROUP BY user_id
    HAVING COUNT(*) > 10
    ORDER BY action_count DESC;
    ```

=== "ROLLUP and CUBE"

    ```sql
    -- Hierarchical aggregation
    SELECT
        DATE(__tx_timestamp) as date,
        action,
        COUNT(*) as count
    FROM audit_log
    GROUP BY ROLLUP(DATE(__tx_timestamp), action)
    ORDER BY date, action;
    ```

---

## Joins with Other Tables

Blockchain tables can be joined with regular tables and other blockchain tables.

### JOIN with Regular Tables

```sql
-- Get user details with audit log
SELECT
    u.username,
    u.email,
    a.action,
    a.__tx_timestamp,
    encode(a.__curr_hash, 'hex') as audit_hash
FROM audit_log a
JOIN users u ON a.user_id = u.id
WHERE a.__tx_timestamp >= NOW() - INTERVAL '1 hour'
ORDER BY a.__tx_timestamp DESC;
```

### JOIN Multiple Blockchain Tables

```sql
-- Cross-reference different blockchain tables
CREATE BLOCKCHAIN TABLE access_log (
    session_id UUID,
    user_id INTEGER,
    resource_id INTEGER,
    action VARCHAR(50)
);

CREATE BLOCKCHAIN TABLE change_log (
    resource_id INTEGER,
    change_type VARCHAR(50),
    changed_by INTEGER
);

-- Find who accessed resources that were recently changed
SELECT DISTINCT
    a.user_id,
    a.resource_id,
    a.action as access_action,
    c.change_type,
    c.__tx_timestamp as change_time,
    a.__tx_timestamp as access_time
FROM access_log a
JOIN change_log c ON a.resource_id = c.resource_id
WHERE a.__tx_timestamp >= c.__tx_timestamp
  AND a.__tx_timestamp <= c.__tx_timestamp + INTERVAL '1 hour'
ORDER BY c.__tx_timestamp DESC;
```

### Complex Multi-Table Joins

=== "Audit Trail Reconstruction"

    ```sql
    -- Complete audit trail with user and resource details
    SELECT
        u.username,
        u.department,
        a.action,
        r.resource_name,
        r.classification,
        a.__tx_timestamp,
        a.__tx_lsn,
        encode(a.__curr_hash, 'hex') as proof_hash
    FROM audit_log a
    JOIN users u ON a.user_id = u.id
    JOIN resources r ON a.resource_id = r.id
    WHERE r.classification = 'confidential'
      AND a.__tx_timestamp >= '2025-01-01'
    ORDER BY a.__tx_lsn;
    ```

=== "LEFT JOIN for Missing Data"

    ```sql
    -- Find audit entries for deleted users
    SELECT
        a.user_id,
        a.action,
        a.__tx_timestamp,
        COALESCE(u.username, '[DELETED USER]') as username,
        CASE
            WHEN u.id IS NULL THEN 'User deleted after audit entry'
            ELSE 'User active'
        END as user_status
    FROM audit_log a
    LEFT JOIN users u ON a.user_id = u.id
    WHERE a.__tx_timestamp >= NOW() - INTERVAL '30 days'
    ORDER BY a.__tx_timestamp DESC;
    ```

---

## Subqueries and CTEs

Use subqueries and Common Table Expressions (CTEs) for complex queries.

### Subquery Examples

```sql
-- Find users with above-average activity
SELECT user_id, COUNT(*) as action_count
FROM audit_log
GROUP BY user_id
HAVING COUNT(*) > (
    SELECT AVG(count)
    FROM (
        SELECT COUNT(*) as count
        FROM audit_log
        GROUP BY user_id
    ) subq
)
ORDER BY action_count DESC;

-- Find latest action per user
SELECT DISTINCT ON (user_id)
    user_id,
    action,
    __tx_timestamp,
    __tx_lsn
FROM audit_log
ORDER BY user_id, __tx_lsn DESC;
```

### CTE (WITH Clause) Examples

=== "Simple CTE"

    ```sql
    -- Analyze user activity patterns
    WITH user_stats AS (
        SELECT
            user_id,
            COUNT(*) as total_actions,
            COUNT(DISTINCT action) as unique_actions,
            MIN(__tx_timestamp) as first_seen,
            MAX(__tx_timestamp) as last_seen
        FROM audit_log
        GROUP BY user_id
    )
    SELECT
        user_id,
        total_actions,
        unique_actions,
        last_seen - first_seen as active_duration
    FROM user_stats
    WHERE total_actions > 5
    ORDER BY total_actions DESC;
    ```

=== "Recursive CTE"

    ```sql
    -- Traverse hash chain recursively
    WITH RECURSIVE chain AS (
        -- Genesis block
        SELECT
            __tx_lsn,
            __curr_hash,
            __prev_hash,
            action,
            1 as depth
        FROM audit_log
        WHERE __prev_hash IS NULL

        UNION ALL

        -- Follow the chain
        SELECT
            a.__tx_lsn,
            a.__curr_hash,
            a.__prev_hash,
            a.action,
            c.depth + 1
        FROM audit_log a
        JOIN chain c ON a.__prev_hash = c.__curr_hash
    )
    SELECT * FROM chain
    ORDER BY depth;
    ```

=== "Multiple CTEs"

    ```sql
    -- Complex analysis with multiple CTEs
    WITH
    -- Recent activity
    recent AS (
        SELECT * FROM audit_log
        WHERE __tx_timestamp >= NOW() - INTERVAL '7 days'
    ),
    -- User summaries
    user_summary AS (
        SELECT
            user_id,
            COUNT(*) as actions,
            array_agg(DISTINCT action) as action_types
        FROM recent
        GROUP BY user_id
    ),
    -- High-value actions
    important_actions AS (
        SELECT * FROM recent
        WHERE action IN ('delete_document', 'export_data', 'admin_access')
    )
    -- Final result
    SELECT
        us.user_id,
        us.actions,
        us.action_types,
        COUNT(ia.action) as important_count
    FROM user_summary us
    LEFT JOIN important_actions ia ON us.user_id = ia.user_id
    GROUP BY us.user_id, us.actions, us.action_types
    ORDER BY important_count DESC, us.actions DESC;
    ```

---

## JSON/JSONB Column Queries

Work with JSON data stored in blockchain tables.

### Basic JSON Queries

```sql
CREATE BLOCKCHAIN TABLE api_requests (
    request_id UUID,
    endpoint VARCHAR(200),
    method VARCHAR(10),
    request_data JSONB,
    response_data JSONB
);

-- Query JSON fields
SELECT
    endpoint,
    request_data->>'user_id' as user_id,
    request_data->'parameters' as parameters,
    response_data->>'status' as status,
    __tx_timestamp
FROM api_requests
WHERE request_data->>'user_id' = '1001'
  AND response_data->>'status' = 'success'
ORDER BY __tx_timestamp DESC;
```

### JSON Aggregations

```sql
-- Aggregate JSON array fields
SELECT
    endpoint,
    COUNT(*) as request_count,
    COUNT(DISTINCT request_data->>'user_id') as unique_users,
    jsonb_agg(DISTINCT response_data->>'status') as status_codes
FROM api_requests
GROUP BY endpoint
ORDER BY request_count DESC;

-- Extract nested JSON
SELECT
    endpoint,
    jsonb_array_elements(request_data->'items')->>'product_id' as product_id,
    __tx_timestamp
FROM api_requests
WHERE endpoint = '/api/purchase';
```

---

## Common Query Patterns

### Pattern 1: Activity Timeline

```sql
-- Complete activity timeline for a user
SELECT
    __tx_lsn as sequence,
    __tx_timestamp as timestamp,
    action,
    resource_id,
    details,
    encode(__curr_hash, 'hex') as proof_hash
FROM audit_log
WHERE user_id = 1001
ORDER BY __tx_lsn;
```

### Pattern 2: Suspicious Activity Detection

```sql
-- Find rapid-fire actions (potential bot)
SELECT
    user_id,
    action,
    COUNT(*) as action_count,
    MIN(__tx_timestamp) as first_occurrence,
    MAX(__tx_timestamp) as last_occurrence,
    MAX(__tx_timestamp) - MIN(__tx_timestamp) as time_span
FROM audit_log
WHERE __tx_timestamp >= NOW() - INTERVAL '1 hour'
GROUP BY user_id, action
HAVING COUNT(*) > 100
   AND MAX(__tx_timestamp) - MIN(__tx_timestamp) < INTERVAL '1 minute'
ORDER BY action_count DESC;
```

### Pattern 3: Data Export for Compliance

```sql
-- Generate compliance report
SELECT
    a.__tx_lsn as sequence_number,
    a.__tx_timestamp as timestamp_utc,
    encode(a.__curr_hash, 'hex') as cryptographic_proof,
    u.username,
    u.email,
    a.action,
    r.resource_name,
    r.classification,
    a.ip_address,
    a.details
FROM audit_log a
JOIN users u ON a.user_id = u.id
LEFT JOIN resources r ON a.resource_id = r.id
WHERE a.__tx_timestamp BETWEEN '2025-01-01' AND '2025-12-31'
  AND r.classification IN ('confidential', 'restricted')
ORDER BY a.__tx_lsn;
```

---

## Performance Tips

### Tip 1: Use Indexes Strategically

```sql
-- Index frequently filtered columns
CREATE INDEX idx_audit_timestamp ON audit_log(__tx_timestamp);
CREATE INDEX idx_audit_user_action ON audit_log(user_id, action);

-- Partial index for specific queries
CREATE INDEX idx_audit_admin_actions ON audit_log(user_id, __tx_timestamp)
WHERE action IN ('admin_access', 'security_override');

-- GIN index for JSON queries
CREATE INDEX idx_api_request_data ON api_requests USING GIN (request_data);
```

### Tip 2: Limit Result Sets

```sql
-- Always use LIMIT for large result sets
SELECT * FROM audit_log
ORDER BY __tx_lsn DESC
LIMIT 100;

-- Use pagination
SELECT * FROM audit_log
ORDER BY __tx_lsn
LIMIT 100 OFFSET 1000;
```

### Tip 3: Analyze Query Plans

```sql
-- Check query performance
EXPLAIN ANALYZE
SELECT * FROM audit_log
WHERE user_id = 1001
  AND __tx_timestamp >= NOW() - INTERVAL '30 days';
```

---

## Common Pitfalls

### Pitfall 1: SELECT * Performance

!!! warning "Issue"
    `SELECT *` on large blockchain tables can be slow due to system column overhead.

!!! success "Solution"
    Select only needed columns:
    ```sql
    -- Instead of SELECT *
    SELECT user_id, action, __tx_timestamp
    FROM audit_log;
    ```

### Pitfall 2: Missing Indexes

!!! warning "Issue"
    Queries without indexes on WHERE clauses cause full table scans.

!!! success "Solution"
    Create appropriate indexes:
    ```sql
    CREATE INDEX idx_audit_user ON audit_log(user_id);
    ```

### Pitfall 3: Implicit Type Conversions

!!! warning "Issue"
    ```sql
    -- This prevents index usage
    SELECT * FROM audit_log
    WHERE user_id::TEXT = '1001';
    ```

!!! success "Solution"
    ```sql
    -- Use correct types
    SELECT * FROM audit_log
    WHERE user_id = 1001;
    ```

---

## Next Steps

- **[Query Anchoring](query-anchoring.md)** - Anchor complex query results for verification
- **[Verification](verification.md)** - Verify hash chain integrity
- **[SQL Functions Reference](../api/sql-functions-reference.md)** - Complete API documentation

---

## See Also

- [Architecture: Blockchain Access Method](../architecture/blockchain-access-method.md)
- [Examples: Complex Queries](../examples/index.md)
- [Performance Tuning Guide](../deployment/configuration.md)
