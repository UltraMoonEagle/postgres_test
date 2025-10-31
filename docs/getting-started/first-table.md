# Your First Blockchain Table

**One-minute summary:**

- Create blockchain tables with `CREATE BLOCKCHAIN TABLE` syntax
- Only `INSERT` operations are allowed (no UPDATE/DELETE)
- Nine system columns are automatically added
- Data is cryptographically linked in a tamper-evident chain

---

## Creating a Blockchain Table

### Basic Syntax

```sql
CREATE BLOCKCHAIN TABLE table_name (
    column_name data_type [constraints],
    ...
);
```

### Example: Simple Audit Log

Let's create a basic audit log to track user actions:

```sql
CREATE BLOCKCHAIN TABLE audit_log (
    user_id INTEGER NOT NULL,
    action VARCHAR(200) NOT NULL,
    ip_address INET,
    details JSONB,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

**What happens behind the scenes:**

1. PostgreSQL creates the table with your specified columns
2. **Nine system columns** are automatically added (prefixed with `__`)
3. The table uses the **blockchain table access method**
4. **Immutability protections** are activated

---

## Understanding System Columns

Every blockchain table automatically includes these system columns:

| Column | Type | Purpose | Example Value |
|--------|------|---------|---------------|
| `__row_id` | UUID | Globally unique row identifier | `550e8400-e29b-41d4-a716-446655440000` |
| `__curr_hash` | BYTEA | SHA-256 hash of this block | `\xabcdef01...` (32 bytes) |
| `__prev_hash` | BYTEA | SHA-256 hash of previous block | `\x9876543...` (32 bytes) |
| `__tx_type` | TEXT | Transaction type (always "INSERT") | `INSERT` |
| `__tx_lsn` | INT8 | Global counter value (total ordering) | `42` |
| `__tx_origin` | UUID | Transaction originator (reserved) | `NULL` |
| `__tx_version` | INT4 | Row version (currently always 1) | `1` |
| `__is_latest` | BOOL | Latest version flag (always TRUE) | `t` |
| `__tx_timestamp` | TIMESTAMPTZ | Transaction timestamp | `2025-10-31 14:32:18.456789+00` |

!!! info "Hidden by Default"
    System columns are **hidden from `SELECT *` queries** but can be accessed explicitly by name.

---

## Inserting Data

### Basic Insert

```sql
INSERT INTO audit_log (user_id, action, ip_address, details)
VALUES (
    1001,
    'login',
    '192.168.1.100'::INET,
    '{"browser": "Chrome", "version": "118"}'::JSONB
);
```

**Output:**

```
INSERT 0 1
```

### Batch Insert

```sql
INSERT INTO audit_log (user_id, action, ip_address) VALUES
    (1001, 'login', '192.168.1.100'),
    (1002, 'logout', '192.168.1.101'),
    (1001, 'view_page', '192.168.1.100'),
    (1003, 'purchase', '192.168.1.102'),
    (1002, 'login', '192.168.1.101');
```

### Insert from SELECT

```sql
-- Copy data from another table into blockchain table
INSERT INTO audit_log (user_id, action, ip_address)
SELECT user_id, event_type, ip FROM legacy_events
WHERE event_date >= '2025-01-01';
```

---

## Querying Data

### Basic Query (System Columns Hidden)

```sql
SELECT * FROM audit_log
ORDER BY created_at DESC
LIMIT 5;
```

**Output:**

```
 user_id |   action   |   ip_address    |              details               |        created_at
---------+------------+-----------------+------------------------------------+---------------------------
    1003 | purchase   | 192.168.1.102   |                                    | 2025-10-31 14:35:22.123
    1002 | login      | 192.168.1.101   |                                    | 2025-10-31 14:35:22.123
    1001 | view_page  | 192.168.1.100   |                                    | 2025-10-31 14:35:22.123
    1002 | logout     | 192.168.1.101   |                                    | 2025-10-31 14:35:22.123
    1001 | login      | 192.168.1.100   | {"browser": "Chrome", "version": ...} | 2025-10-31 14:32:18.456
```

### Query with System Columns

```sql
SELECT
    user_id,
    action,
    __tx_lsn AS sequence_number,
    __tx_timestamp,
    encode(__curr_hash, 'hex') AS current_hash
FROM audit_log
ORDER BY __tx_lsn DESC
LIMIT 5;
```

**Output:**

```
 user_id |   action   | sequence_number |        __tx_timestamp         |                          current_hash
---------+------------+-----------------+-------------------------------+----------------------------------------------------------------
    1003 | purchase   |               5 | 2025-10-31 14:35:22.123456+00 | a1b2c3d4e5f6789012345678901234567890abcdef1234567890abcdef12
    1002 | login      |               4 | 2025-10-31 14:35:22.123456+00 | b2c3d4e5f6789012345678901234567890abcdef1234567890abcdef1234
    1001 | view_page  |               3 | 2025-10-31 14:35:22.123456+00 | c3d4e5f6789012345678901234567890abcdef1234567890abcdef123456
    1002 | logout     |               2 | 2025-10-31 14:35:22.123456+00 | d4e5f6789012345678901234567890abcdef1234567890abcdef12345678
    1001 | login      |               1 | 2025-10-31 14:32:18.456789+00 | e5f6789012345678901234567890abcdef1234567890abcdef1234567890
```

### Verify Hash Chain Linkage

```sql
-- Show how each row links to the previous one
SELECT
    __tx_lsn AS seq,
    left(encode(__prev_hash, 'hex'), 16) AS prev_hash_prefix,
    left(encode(__curr_hash, 'hex'), 16) AS curr_hash_prefix,
    user_id,
    action
FROM audit_log
ORDER BY __tx_lsn;
```

**Output:**

```
 seq | prev_hash_prefix |  curr_hash_prefix  | user_id |   action
-----+------------------+--------------------+---------+------------
   1 | 0000000000000000 | e5f6789012345678   |    1001 | login
   2 | e5f6789012345678 | d4e5f6789012345678 |    1002 | logout
   3 | d4e5f6789012345678| c3d4e5f6789012345678|    1001 | view_page
   4 | c3d4e5f6789012345678| b2c3d4e5f6789012345678|    1002 | login
   5 | b2c3d4e5f6789012345678| a1b2c3d4e5f6789012345678|    1003 | purchase
```

!!! success "Perfect Chain Linkage"
    Notice how each row's `__prev_hash` matches the previous row's `__curr_hash`. This forms an unbreakable cryptographic chain!

---

## What You CANNOT Do

Blockchain tables enforce immutability. The following operations are **blocked**:

### UPDATE (Blocked)

```sql
UPDATE audit_log SET action = 'modified' WHERE user_id = 1001;
```

**Error:**

```
ERROR:  UPDATE operation not allowed on blockchain table "audit_log"
HINT:  Blockchain tables are immutable and only support INSERT operations.
```

### DELETE (Blocked)

```sql
DELETE FROM audit_log WHERE user_id = 1001;
```

**Error:**

```
ERROR:  DELETE operation not allowed on blockchain table "audit_log"
HINT:  Blockchain tables are immutable and only support INSERT operations.
```

### TRUNCATE (Blocked)

```sql
TRUNCATE TABLE audit_log;
```

**Error:**

```
ERROR:  TRUNCATE operation not allowed on blockchain table "audit_log"
HINT:  Blockchain tables are immutable. Use DROP TABLE to remove entirely.
```

### Schema Alterations (Restricted)

```sql
-- This is BLOCKED
ALTER TABLE audit_log ADD COLUMN new_col TEXT;
```

**Error:**

```
ERROR:  ALTER TABLE operation not allowed on blockchain table "audit_log"
HINT:  Blockchain table schema cannot be modified after creation.
```

---

## Verify Blockchain Integrity

### Option 1: Manual Chain Verification

```sql
-- Check for hash chain breaks
WITH chain_check AS (
    SELECT
        __tx_lsn,
        __curr_hash,
        __prev_hash,
        LAG(__curr_hash) OVER (ORDER BY __tx_lsn) AS actual_prev_hash
    FROM audit_log
)
SELECT
    __tx_lsn,
    CASE
        WHEN __tx_lsn = 1 THEN 'Genesis block (OK)'
        WHEN __prev_hash = actual_prev_hash THEN 'Hash chain valid ✓'
        ELSE 'HASH CHAIN BROKEN! ✗'
    END AS chain_status
FROM chain_check
ORDER BY __tx_lsn;
```

**Expected output (all valid):**

```
 __tx_lsn |       chain_status
----------+--------------------------
        1 | Genesis block (OK)
        2 | Hash chain valid ✓
        3 | Hash chain valid ✓
        4 | Hash chain valid ✓
        5 | Hash chain valid ✓
```

### Option 2: Use Built-in Verification Function

```sql
-- If available in your installation
SELECT * FROM verify_blockchain_chain('audit_log');
```

---

## Common Patterns

### Pattern 1: Immutable Event Log

```sql
CREATE BLOCKCHAIN TABLE event_log (
    event_id UUID DEFAULT gen_random_uuid(),
    event_type VARCHAR(50) NOT NULL,
    entity_type VARCHAR(50) NOT NULL,
    entity_id BIGINT NOT NULL,
    event_data JSONB NOT NULL,
    triggered_by VARCHAR(100),
    event_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Index on frequently queried columns (yes, you can create indexes!)
CREATE INDEX idx_event_entity ON event_log(entity_type, entity_id);
CREATE INDEX idx_event_time ON event_log(event_time);
```

### Pattern 2: Financial Transaction Ledger

```sql
CREATE BLOCKCHAIN TABLE transaction_ledger (
    transaction_id UUID DEFAULT gen_random_uuid(),
    account_from VARCHAR(50) NOT NULL,
    account_to VARCHAR(50) NOT NULL,
    amount NUMERIC(19, 4) NOT NULL CHECK (amount > 0),
    currency VARCHAR(3) NOT NULL,
    transaction_type VARCHAR(20) NOT NULL,
    reference_number VARCHAR(100),
    notes TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

### Pattern 3: Compliance Document Log

```sql
CREATE BLOCKCHAIN TABLE document_access_log (
    access_id UUID DEFAULT gen_random_uuid(),
    document_id VARCHAR(100) NOT NULL,
    document_type VARCHAR(50) NOT NULL,
    user_id INTEGER NOT NULL,
    access_type VARCHAR(20) NOT NULL, -- 'view', 'download', 'print'
    ip_address INET NOT NULL,
    user_agent TEXT,
    access_timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    session_id VARCHAR(100),
    organization_id INTEGER
);
```

---

## Performance Considerations

### Hash Computation Overhead

- **Per-row INSERT cost:** ~2-5 microseconds for hash computation
- **Total INSERT overhead:** ~5-10% compared to regular tables
- **Hash chain lookup:** <1 microsecond (cached) to ~50 microseconds (disk)

### When to Use Blockchain Tables

✅ **Good use cases:**

- Audit logs (security, compliance)
- Financial transactions
- Document access tracking
- Regulatory compliance data
- Supply chain events
- Medical record access logs

❌ **Poor use cases:**

- Frequently updated data
- Temporary/staging tables
- High-churn data (user sessions, caches)
- Large binary objects (use references instead)

### Optimization Tips

1. **Batch inserts** when possible (reduces per-transaction overhead)
2. **Use appropriate indexes** on query columns (blockchain tables support indexes!)
3. **Partition large tables** by time range if supported
4. **Archive old data** to separate tables if chain doesn't need to span years

---

## Monitoring Your Blockchain Table

### Check Table Statistics

```sql
SELECT
    schemaname,
    tablename,
    n_tup_ins AS total_inserts,
    n_tup_upd AS attempted_updates,  -- Should always be 0!
    n_tup_del AS attempted_deletes,  -- Should always be 0!
    n_live_tup AS total_rows,
    last_vacuum,
    last_analyze
FROM pg_stat_user_tables
WHERE tablename = 'audit_log';
```

### Check Counter Range

```sql
SELECT
    MIN(__tx_lsn) AS first_counter,
    MAX(__tx_lsn) AS last_counter,
    COUNT(*) AS total_rows,
    MAX(__tx_lsn) - MIN(__tx_lsn) + 1 AS counter_span,
    MAX(__tx_lsn) - MIN(__tx_lsn) + 1 - COUNT(*) AS counter_gaps
FROM audit_log;
```

**Understanding counter gaps:**

- Gaps occur when transactions **abort** or **rollback**
- Gaps are **intentional** and part of the immutable audit trail
- They indicate attempted transactions that failed

---

## Next Steps

- [:octicons-arrow-right-24: User Guide](../user-guide/index.md) - Learn advanced usage patterns
- [:octicons-arrow-right-24: Query Anchoring](../user-guide/query-anchoring.md) - Anchor query results
- [:octicons-arrow-right-24: Examples](../examples/index.md) - See real-world examples
- [:octicons-arrow-right-24: Best Practices](../user-guide/best-practices.md) - Production recommendations

---

## Quick Reference

### Create blockchain table
```sql
CREATE BLOCKCHAIN TABLE name (columns...);
```

### Insert data
```sql
INSERT INTO name (cols...) VALUES (...);
```

### Query with system columns
```sql
SELECT *, __tx_lsn, __curr_hash FROM name;
```

### Check chain integrity
```sql
SELECT * FROM verify_blockchain_chain('name');
```

### View table stats
```sql
\d+ name  -- In psql
```

