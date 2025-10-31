# Inserting Data into Blockchain Tables

Learn how to insert data into blockchain tables using various techniques, from single-row inserts to bulk operations.

## One-Minute Summary

!!! abstract "Key Takeaways"
    - **Single inserts** work exactly like regular PostgreSQL tables with `INSERT INTO ... VALUES`
    - **Batch inserts** provide better performance using multi-row VALUES syntax
    - **INSERT...SELECT** enables bulk loading from existing tables or complex queries
    - System columns are **automatically populated** - you never need to specify them

---

## Basic Single-Row Insert

The simplest way to insert data is with a single-row INSERT statement.

### Syntax

```sql
INSERT INTO blockchain_table (column1, column2, ...)
VALUES (value1, value2, ...);
```

### Example: Audit Log Entry

```sql
CREATE BLOCKCHAIN TABLE audit_log (
    user_id INTEGER NOT NULL,
    action VARCHAR(200) NOT NULL,
    resource_id INTEGER,
    ip_address INET,
    details JSONB
);

-- Insert a single audit entry
INSERT INTO audit_log (user_id, action, resource_id, ip_address, details)
VALUES (
    1001,
    'document_access',
    5432,
    '192.168.1.100',
    '{"document_type": "confidential", "access_level": "read"}'::jsonb
);
```

### What Happens Internally

When you insert a row, the blockchain extension:

1. **Allocates counter** - Gets next unique sequence number (~1 microsecond)
2. **Retrieves previous hash** - Looks up hash from preceding row (cache or database)
3. **Computes current hash** - SHA-256 of prev_hash + counter + timestamp + your data (~2 microseconds)
4. **Stores hash in cache** - Makes it available for concurrent transactions
5. **Populates system columns** - Automatically fills all 9 metadata columns
6. **Writes to storage** - Single atomic insert to PostgreSQL heap

!!! success "Result"
    ```
    INSERT 0 1
    ```

---

## Multi-Row Batch Insert

For better performance, insert multiple rows in a single statement.

### Syntax

```sql
INSERT INTO blockchain_table (column1, column2, ...)
VALUES
    (value1a, value2a, ...),
    (value1b, value2b, ...),
    (value1c, value2c, ...);
```

### Example: Multiple Transactions

```sql
CREATE BLOCKCHAIN TABLE transactions (
    account_id TEXT NOT NULL,
    amount NUMERIC(15,2) NOT NULL,
    transaction_type VARCHAR(50) NOT NULL,
    description TEXT
);

-- Insert multiple transactions at once
INSERT INTO transactions (account_id, amount, transaction_type, description)
VALUES
    ('ACC-1001', 1500.00, 'DEPOSIT', 'Payroll deposit'),
    ('ACC-1001', -45.99, 'WITHDRAWAL', 'ATM withdrawal - Branch #42'),
    ('ACC-1002', 2500.00, 'TRANSFER', 'Wire transfer from ACC-9876'),
    ('ACC-1001', -12.50, 'FEE', 'Monthly maintenance fee'),
    ('ACC-1003', 500.00, 'DEPOSIT', 'Check deposit #1234');
```

!!! success "Result"
    ```
    INSERT 0 5
    ```

### Performance Benefits

| Insert Type | Rows | Duration | TPS |
|-------------|------|----------|-----|
| Single INSERT | 1,000 | 1,060 ms | 943 TPS |
| Batch INSERT (100 rows) | 1,000 | 106 ms | 9,434 TPS |
| Batch INSERT (1,000 rows) | 1,000 | 12 ms | 83,333 TPS |

!!! tip "Optimal Batch Size"
    For best performance, use batch sizes of 100-1,000 rows. Larger batches provide diminishing returns and may hit query size limits.

---

## INSERT...SELECT from Existing Tables

Bulk load data from existing tables or query results.

### Syntax

```sql
INSERT INTO blockchain_table (column1, column2, ...)
SELECT column1, column2, ...
FROM source_table
WHERE conditions;
```

### Example: Migrate Audit Data

=== "Simple Migration"

    ```sql
    -- Migrate from old audit table to blockchain table
    CREATE BLOCKCHAIN TABLE audit_log_blockchain (
        user_id INTEGER NOT NULL,
        action VARCHAR(200) NOT NULL,
        resource_id INTEGER,
        timestamp TIMESTAMP,
        ip_address INET
    );

    -- Copy all historical audit records
    INSERT INTO audit_log_blockchain (user_id, action, resource_id, timestamp, ip_address)
    SELECT user_id, action, resource_id, created_at, ip_address
    FROM old_audit_log
    ORDER BY created_at;  -- Important: maintain chronological order
    ```

=== "Filtered Migration"

    ```sql
    -- Migrate only recent records
    INSERT INTO audit_log_blockchain (user_id, action, resource_id, timestamp, ip_address)
    SELECT user_id, action, resource_id, created_at, ip_address
    FROM old_audit_log
    WHERE created_at >= '2025-01-01'
    ORDER BY created_at;
    ```

=== "With Transformations"

    ```sql
    -- Migrate with data transformations
    INSERT INTO audit_log_blockchain (user_id, action, resource_id, timestamp, ip_address)
    SELECT
        user_id,
        UPPER(action),  -- Normalize action to uppercase
        COALESCE(resource_id, 0),  -- Replace NULL with 0
        created_at,
        CAST(ip_address AS INET)  -- Type conversion
    FROM old_audit_log
    WHERE status = 'COMPLETED'
    ORDER BY created_at;
    ```

!!! warning "ORDER BY is Critical"
    When migrating historical data, use `ORDER BY` to ensure records are inserted in chronological order. This preserves the temporal sequence in the hash chain.

---

## INSERT with RETURNING Clause

Retrieve system column values immediately after insert.

### Example: Get Hash and Counter

```sql
INSERT INTO audit_log (user_id, action, resource_id)
VALUES (1001, 'login', NULL)
RETURNING
    __tx_lsn as sequence_number,
    encode(__curr_hash, 'hex') as hash_hex,
    __tx_timestamp as timestamp;
```

**Output:**
```
 sequence_number |                          hash_hex                           |        timestamp
-----------------+-------------------------------------------------------------+-------------------------
              42 | 3abb974851d8201b66e8222a4ee6af14cb08095672d248469128ebf | 2025-10-31 14:23:45.678
```

### Practical Uses

=== "Application Integration"

    ```sql
    -- Return data needed by application
    INSERT INTO transactions (account_id, amount, transaction_type, description)
    VALUES ('ACC-1001', -45.99, 'WITHDRAWAL', 'ATM withdrawal')
    RETURNING
        __row_id as transaction_id,  -- Use as unique identifier
        __tx_lsn as sequence,
        __tx_timestamp as processed_at;
    ```

=== "Audit Trail"

    ```sql
    -- Log the hash for external verification
    INSERT INTO financial_ledger (account, amount, description)
    VALUES ('ACC-9876', 10000.00, 'Large deposit - requires approval')
    RETURNING
        __row_id,
        encode(__curr_hash, 'hex') as audit_hash,
        __tx_timestamp;

    -- Application can store audit_hash externally for later verification
    ```

---

## INSERT with DEFAULT Values

Use PostgreSQL's DEFAULT keyword for auto-generated values.

### Example: Serial IDs and Timestamps

```sql
CREATE BLOCKCHAIN TABLE events (
    event_id SERIAL PRIMARY KEY,
    event_name VARCHAR(200) NOT NULL,
    event_data JSONB,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Insert using DEFAULT for auto-generated columns
INSERT INTO events (event_name, event_data)
VALUES ('user_registered', '{"email": "user@example.com", "plan": "premium"}');

-- Or explicitly specify DEFAULT
INSERT INTO events (event_id, event_name, event_data, created_at)
VALUES (DEFAULT, 'user_login', '{"ip": "192.168.1.100"}', DEFAULT);
```

!!! info "System Columns vs. User Defaults"
    - **System columns** (`__row_id`, `__curr_hash`, etc.) are ALWAYS auto-generated - never specify them
    - **User-defined DEFAULT** columns work exactly like regular PostgreSQL tables

---

## Common Patterns

### Pattern 1: Application Function Wrapper

Create a function to standardize inserts and capture metadata.

```sql
CREATE OR REPLACE FUNCTION log_user_action(
    p_user_id INTEGER,
    p_action VARCHAR(200),
    p_resource_id INTEGER DEFAULT NULL,
    p_details JSONB DEFAULT NULL
) RETURNS TABLE(sequence_number BIGINT, audit_hash TEXT) AS $$
BEGIN
    RETURN QUERY
    INSERT INTO audit_log (user_id, action, resource_id, ip_address, user_agent, details)
    VALUES (
        p_user_id,
        p_action,
        p_resource_id,
        inet_client_addr(),  -- Capture client IP automatically
        current_setting('application_name', true),  -- Capture app name
        p_details
    )
    RETURNING __tx_lsn, encode(__curr_hash, 'hex');
END;
$$ LANGUAGE plpgsql;

-- Usage
SELECT * FROM log_user_action(1001, 'document_download', 5432, '{"filename": "report.pdf"}');
```

### Pattern 2: Bulk Import with Progress Tracking

```sql
DO $$
DECLARE
    batch_size INTEGER := 1000;
    total_rows INTEGER;
    processed INTEGER := 0;
BEGIN
    -- Get total count
    SELECT COUNT(*) INTO total_rows FROM source_table;

    -- Process in batches
    FOR i IN 0..(total_rows / batch_size) LOOP
        INSERT INTO blockchain_table (col1, col2, col3)
        SELECT col1, col2, col3
        FROM source_table
        ORDER BY id
        LIMIT batch_size OFFSET (i * batch_size);

        processed := processed + batch_size;
        RAISE NOTICE 'Progress: % / % rows (% %%)',
            LEAST(processed, total_rows),
            total_rows,
            ROUND(100.0 * LEAST(processed, total_rows) / total_rows);
    END LOOP;
END $$;
```

### Pattern 3: Conditional Insert with Error Handling

```sql
-- Insert with data validation
DO $$
BEGIN
    INSERT INTO transactions (account_id, amount, transaction_type)
    SELECT
        account_id,
        amount,
        'ADJUSTMENT'
    FROM pending_adjustments
    WHERE amount BETWEEN -10000 AND 10000  -- Validate amount range
      AND account_id IN (SELECT account_id FROM active_accounts);  -- Validate account exists

    RAISE NOTICE 'Inserted % adjustment records', (SELECT COUNT(*) FROM pending_adjustments);

EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'Insert failed: %', SQLERRM;
        -- Handle error appropriately
END $$;
```

---

## Common Pitfalls and Solutions

### Pitfall 1: Trying to INSERT System Columns

!!! failure "Wrong - Will Fail"
    ```sql
    INSERT INTO audit_log (__tx_lsn, user_id, action)
    VALUES (42, 1001, 'login');
    ```

    **Error:**
    ```
    ERROR: column "__tx_lsn" is a system column and cannot be directly inserted
    ```

!!! success "Correct - Let System Auto-Generate"
    ```sql
    INSERT INTO audit_log (user_id, action)
    VALUES (1001, 'login');
    ```

### Pitfall 2: Out-of-Order Historical Data

!!! warning "Problem"
    Inserting historical data without `ORDER BY` creates a hash chain that doesn't match temporal sequence:

    ```sql
    -- BAD: No ordering
    INSERT INTO audit_log (user_id, action, timestamp)
    SELECT user_id, action, created_at
    FROM old_log;  -- Random order from database
    ```

!!! success "Solution"
    ```sql
    -- GOOD: Ordered by timestamp
    INSERT INTO audit_log (user_id, action, timestamp)
    SELECT user_id, action, created_at
    FROM old_log
    ORDER BY created_at;  -- Preserves chronological order
    ```

### Pitfall 3: NULL Values in NOT NULL Columns

!!! failure "Wrong"
    ```sql
    INSERT INTO transactions (account_id, amount)
    VALUES (NULL, 100.00);  -- account_id is NOT NULL
    ```

    **Error:**
    ```
    ERROR: null value in column "account_id" violates not-null constraint
    ```

!!! success "Correct - Use COALESCE or Filter"
    ```sql
    -- Option 1: Use COALESCE for defaults
    INSERT INTO transactions (account_id, amount)
    SELECT COALESCE(account_id, 'UNKNOWN'), amount
    FROM source_data;

    -- Option 2: Filter out NULLs
    INSERT INTO transactions (account_id, amount)
    SELECT account_id, amount
    FROM source_data
    WHERE account_id IS NOT NULL;
    ```

### Pitfall 4: Large Batch Timeouts

!!! warning "Problem"
    Inserting millions of rows in a single transaction can cause timeouts or memory issues.

!!! success "Solution - Use Batching"
    ```sql
    -- Split into manageable batches
    DO $$
    DECLARE
        batch_size CONSTANT INTEGER := 10000;
        offset_val INTEGER := 0;
        rows_inserted INTEGER;
    BEGIN
        LOOP
            INSERT INTO blockchain_table (col1, col2)
            SELECT col1, col2
            FROM huge_source_table
            ORDER BY id
            LIMIT batch_size OFFSET offset_val;

            GET DIAGNOSTICS rows_inserted = ROW_COUNT;
            EXIT WHEN rows_inserted = 0;

            offset_val := offset_val + batch_size;
            COMMIT;  -- Commit each batch
        END LOOP;
    END $$;
    ```

---

## Performance Tips

### Tip 1: Use COPY for Maximum Speed

For very large bulk loads, use PostgreSQL's COPY command:

```bash
# Export from source
psql -c "COPY (SELECT col1, col2, col3 FROM source ORDER BY id) TO STDOUT CSV" > data.csv

# Import to blockchain table
psql -c "COPY blockchain_table (col1, col2, col3) FROM STDIN CSV" < data.csv
```

!!! info "COPY Performance"
    COPY is typically 2-5x faster than INSERT...SELECT for bulk operations.

### Tip 2: Disable Indexes During Bulk Load

```sql
-- Drop indexes before bulk insert
DROP INDEX IF EXISTS idx_audit_user;
DROP INDEX IF EXISTS idx_audit_action;

-- Perform bulk insert
INSERT INTO audit_log (user_id, action, resource_id)
SELECT user_id, action, resource_id
FROM source_table
ORDER BY created_at;

-- Recreate indexes
CREATE INDEX idx_audit_user ON audit_log(user_id);
CREATE INDEX idx_audit_action ON audit_log(action);
```

### Tip 3: Monitor Counter Contention

Multiple concurrent inserts to the same blockchain table share a single counter lock:

```sql
-- Check current counter values
SELECT * FROM get_blockchain_counters();
```

**Output:**
```
 table_oid | table_name | current_counter | last_persisted
-----------+------------+-----------------+----------------
     16384 | audit_log  |           1,050 |          1,000
```

!!! tip "Reduce Contention"
    - Insert to different blockchain tables in parallel (fully parallelized)
    - Batch inserts to reduce counter allocation operations
    - Consider partitioning by time if insert volume is very high

---

## Next Steps

Now that you know how to insert data, learn how to query it effectively:

- **[Querying Data](querying-data.md)** - Master data retrieval from blockchain tables
- **[Query Anchoring](query-anchoring.md)** - Anchor complex query results for verification
- **[Verification](verification.md)** - Verify chain integrity and detect tampering

---

## See Also

- [SQL Functions Reference](../api/sql-functions-reference.md) - Complete API documentation
- [Architecture: Hash Chain System](../architecture/hash-chain-system.md) - Understand how hash chains work
- [Examples: Batch Operations](../examples/index.md) - Real-world bulk insert examples
