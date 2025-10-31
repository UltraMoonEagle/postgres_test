# System Columns Reference - PostgreSQL Blockchain Extension

## Overview

Every blockchain table automatically includes 9 hidden system columns that provide blockchain functionality, immutability, and tamper-evidence. These columns are automatically managed by the system and cannot be modified by users.

**Total System Columns**: 9
**Storage Overhead**: Approximately 200-250 bytes per row
**Visibility**: Hidden by default, visible when explicitly selected

## System Column Catalog

### Quick Reference Table

| Column Name | Data Type | Size | Purpose | Auto-Generated | Indexed |
|------------|-----------|------|---------|----------------|---------|
| `__row_id` | UUID | 16 bytes | Unique row identifier | Yes | No |
| `__curr_hash` | BYTEA | 36 bytes | Current row hash (SHA-256) | Yes | No |
| `__prev_hash` | BYTEA | 36 bytes | Previous row hash (chain link) | Yes | No |
| `__tx_type` | TEXT | ~10 bytes | Transaction type | Yes | No |
| `__tx_lsn` | BIGINT | 8 bytes | Global counter value | Yes | No |
| `__tx_origin` | UUID | 16 bytes | Transaction origin | Yes | No |
| `__tx_version` | INTEGER | 4 bytes | Version number | Yes | No |
| `__is_latest` | BOOLEAN | 1 byte | Latest version flag | Yes | No |
| `__tx_timestamp` | TIMESTAMPTZ | 8 bytes | Transaction timestamp | Yes | No |

**Total Overhead**: ~135 bytes + VARLENA headers ≈ 200-250 bytes per row

---

## Detailed Column Specifications

### 1. `__row_id` - Unique Row Identifier

**PostgreSQL Type**: `UUID`
**OID**: `UUIDOID` (2950)
**Storage Size**: 16 bytes
**Nullability**: NOT NULL
**Generation**: Random UUID v4 using cryptographically secure random number generator

#### Purpose
Provides a globally unique identifier for each row across all blockchain tables. Used for:
- Row identification and retrieval
- Cross-table references
- Audit trail correlation
- Forensic analysis

#### Generation Algorithm
```c
// Uses PostgreSQL's pg_strong_random() for cryptographic security
// RFC 4122 UUID version 4 format
// Example: 550e8400-e29b-41d4-a716-446655440000
```

#### Example Values
```
550e8400-e29b-41d4-a716-446655440000
6ba7b810-9dad-11d1-80b4-00c04fd430c8
7c9e6679-7425-40de-944b-e07fc1f90ae7
```

#### Usage Examples
```sql
-- Select rows by row_id
SELECT * FROM audit_log WHERE __row_id = '550e8400-e29b-41d4-a716-446655440000';

-- Use in JOINs
SELECT a.*, b.*
FROM audit_log a
JOIN related_data b ON a.__row_id = b.audit_row_id;

-- Count unique rows
SELECT COUNT(DISTINCT __row_id) FROM audit_log;
```

#### Notes
- **Immutable**: Cannot be changed after insertion
- **Unique**: Guaranteed unique across all rows and tables
- **Persistent**: Stable identifier that never changes
- **Performance**: No index by default; create if needed for frequent lookups

---

### 2. `__curr_hash` - Current Row Hash

**PostgreSQL Type**: `BYTEA`
**OID**: `BYTEAOID` (17)
**Storage Size**: 32 bytes (SHA-256) + 4 bytes VARLENA header = 36 bytes
**Nullability**: NOT NULL
**Generation**: SHA-256 cryptographic hash of row data + previous hash + counter + timestamp

#### Purpose
Cryptographic hash that creates the blockchain chain. Provides:
- Tamper-evidence (any data modification breaks the chain)
- Data integrity verification
- Cryptographic proof of data state
- Forward chaining to detect modifications

#### Hash Computation Algorithm
```
__curr_hash = SHA256(
    user_column_1_value ||
    user_column_2_value ||
    ... ||
    user_column_N_value ||
    __prev_hash ||
    __tx_lsn ||
    __tx_timestamp
)
```

#### Example Values
```
\x1a2b3c4d5e6f789a0b1c2d3e4f5678901a2b3c4d5e6f789a0b1c2d3e4f567890
\x9f8e7d6c5b4a39281f0e1d2c3b4a59687f6e5d4c3b2a19080f1e2d3c4b5a6978
\xdeadbeef1234567890abcdef1234567890abcdef1234567890abcdef12345678
```

#### Usage Examples
```sql
-- View hash as hex string
SELECT __row_id, encode(__curr_hash, 'hex') AS current_hash
FROM audit_log;

-- Compare stored vs expected hash
SELECT __tx_lsn, __curr_hash,
       __curr_hash = '\x1a2b3c...'::bytea AS hash_matches
FROM audit_log
WHERE __tx_lsn = 1001;

-- Export hash chain
SELECT __tx_lsn,
       encode(__curr_hash, 'hex') AS curr_hash,
       encode(__prev_hash, 'hex') AS prev_hash
FROM audit_log
ORDER BY __tx_lsn;
```

#### Hash Chain Verification
```sql
-- Verify hash chain integrity (manual method)
WITH chain AS (
  SELECT __tx_lsn, __curr_hash, __prev_hash,
         LAG(__curr_hash) OVER (ORDER BY __tx_lsn) AS expected_prev
  FROM audit_log
)
SELECT __tx_lsn,
       CASE WHEN __prev_hash = expected_prev OR expected_prev IS NULL
            THEN 'VALID' ELSE 'BROKEN' END AS chain_status
FROM chain;
```

#### Notes
- **Algorithm**: SHA-256 (256-bit, 32-byte output)
- **Collision Resistance**: ~2^256 security level
- **Performance**: Computed once at insert time
- **Immutable**: Cannot be modified or recalculated

---

### 3. `__prev_hash` - Previous Row Hash

**PostgreSQL Type**: `BYTEA`
**OID**: `BYTEAOID` (17)
**Storage Size**: 32 bytes (SHA-256) + 4 bytes VARLENA header = 36 bytes
**Nullability**: NULL for first row, NOT NULL for subsequent rows
**Source**: Retrieved from hash cache or previous committed row

#### Purpose
Creates the blockchain chain by linking each row to the previous row. Enables:
- Chain traversal (backward)
- Tamper detection (any modification breaks all subsequent links)
- Historical integrity verification
- Fork detection

#### Special Cases
```sql
-- First row in table (no previous hash)
__prev_hash = NULL (or all zeros: \x0000...0000)

-- Subsequent rows
__prev_hash = __curr_hash of previous row (by __tx_lsn order)
```

#### Example Values
```
\x0000000000000000000000000000000000000000000000000000000000000000  -- First row
\x1a2b3c4d5e6f789a0b1c2d3e4f5678901a2b3c4d5e6f789a0b1c2d3e4f567890  -- Row 2
\x9f8e7d6c5b4a39281f0e1d2c3b4a59687f6e5d4c3b2a19080f1e2d3c4b5a6978  -- Row 3
```

#### Usage Examples
```sql
-- Find first row (genesis block)
SELECT * FROM audit_log
WHERE __prev_hash IS NULL
   OR __prev_hash = '\x0000000000000000000000000000000000000000000000000000000000000000'::bytea;

-- Verify chain continuity
SELECT a.__tx_lsn, a.__curr_hash, a.__prev_hash, b.__curr_hash AS actual_prev,
       a.__prev_hash = b.__curr_hash AS chain_valid
FROM audit_log a
LEFT JOIN audit_log b ON b.__tx_lsn = a.__tx_lsn - 1
ORDER BY a.__tx_lsn;

-- Find broken chain links
SELECT a.__tx_lsn
FROM audit_log a
LEFT JOIN audit_log b ON b.__tx_lsn = a.__tx_lsn - 1
WHERE a.__prev_hash != b.__curr_hash AND a.__tx_lsn > 1;
```

#### Hash Cache Mechanism
```
Transaction 1:                Transaction 2:
1. Get counter = 100         3. Get counter = 101
2. Compute & cache hash      4. Read hash(100) from cache
   Store in shared memory    5. Use as __prev_hash
                             6. Compute & cache hash(101)
```

#### Notes
- **Concurrency**: Retrieved from shared memory cache for uncommitted blocks
- **Cache Size**: Maximum 10,000 uncommitted hashes cached
- **Performance**: Lock-free reads, minimal contention
- **Verification**: Must match previous row's `__curr_hash`

---

### 4. `__tx_type` - Transaction Type

**PostgreSQL Type**: `TEXT`
**OID**: `TEXTOID` (25)
**Storage Size**: Variable, typically 6-10 bytes + VARLENA overhead
**Nullability**: NOT NULL
**Generation**: Automatically set based on operation type

#### Purpose
Records the type of database operation that created the row. Used for:
- Audit trail analysis
- Transaction classification
- Compliance reporting
- Forensic investigation

#### Valid Values
| Value | Description | Frequency |
|-------|-------------|-----------|
| `INSERT` | New row insertion | Most common |
| `UPDATE` | Logical update (new version) | Not yet implemented |
| `DELETE` | Logical delete (tombstone) | Not yet implemented |
| `SYSTEM` | System-generated row | Rare |
| `IMPORT` | Data import operation | Migration only |

#### Current Implementation
```sql
-- Currently always set to 'INSERT' due to immutability
__tx_type = 'INSERT'
```

#### Usage Examples
```sql
-- Count by transaction type
SELECT __tx_type, COUNT(*)
FROM audit_log
GROUP BY __tx_type;

-- Filter by transaction type
SELECT * FROM audit_log WHERE __tx_type = 'INSERT';

-- Audit report with transaction types
SELECT __tx_timestamp::date AS date,
       __tx_type,
       COUNT(*) AS transaction_count
FROM audit_log
GROUP BY date, __tx_type
ORDER BY date DESC;
```

#### Future Enhancements
When logical updates/deletes are implemented:
```sql
-- Logical update creates new row with __tx_type = 'UPDATE'
-- Original row's __is_latest = false
-- New row's __is_latest = true

-- Logical delete creates tombstone with __tx_type = 'DELETE'
```

#### Notes
- **Immutability**: Currently always `INSERT` (physical updates blocked)
- **Future**: Will support logical updates via versioning
- **Storage**: Compressed if table has many rows with same value
- **Indexing**: Consider GIN index for audit queries

---

### 5. `__tx_lsn` - Global Counter Value

**PostgreSQL Type**: `BIGINT` (INT8)
**OID**: `INT8OID` (20)
**Storage Size**: 8 bytes
**Nullability**: NOT NULL
**Generation**: Monotonically increasing global counter per table
**Range**: 1 to 9,223,372,036,854,775,807

#### Purpose
Provides total ordering of all rows in a blockchain table. Replaces LSN (Log Sequence Number) with a simpler, guaranteed-sequential counter. Used for:
- Row ordering (insertion sequence)
- Hash chain sequencing
- Range queries
- Replication coordination
- Audit trail ordering

#### Counter Characteristics
- **Monotonic**: Always increasing, never decreases
- **Sequential**: No gaps in normal operation
- **Per-Table**: Each blockchain table has independent counter
- **Persistent**: Survives database restarts
- **Thread-Safe**: Atomic operations with LWLock protection
- **Cached**: Stored in shared memory for performance

#### Counter Management
```c
// Shared memory structure
typedef struct BlockchainCounterEntry {
    Oid    table_oid;        // Table identifier
    uint64 counter_value;    // Current counter
    uint64 last_persisted;   // Last value saved to disk
    LWLock lock;            // Concurrency control
} BlockchainCounterEntry;

// File persistence
File: $PGDATA/global/blockchain_counters
Format: Binary, table_oid -> counter_value mapping
```

#### Example Values
```
1
2
3
...
1000
1001
...
9223372036854775807  -- Maximum value
```

#### Usage Examples
```sql
-- Get current max counter
SELECT MAX(__tx_lsn) AS current_counter FROM audit_log;

-- Get rows in insertion order
SELECT * FROM audit_log ORDER BY __tx_lsn;

-- Get rows in specific counter range
SELECT * FROM audit_log
WHERE __tx_lsn BETWEEN 1000 AND 2000
ORDER BY __tx_lsn;

-- Get recent N rows
SELECT * FROM audit_log
ORDER BY __tx_lsn DESC
LIMIT 100;

-- Find gaps in counter sequence (detect issues)
SELECT __tx_lsn,
       __tx_lsn - LAG(__tx_lsn) OVER (ORDER BY __tx_lsn) AS gap
FROM audit_log
WHERE __tx_lsn - LAG(__tx_lsn) OVER (ORDER BY __tx_lsn) > 1;
```

#### Performance Considerations
```sql
-- Consider creating index for range queries
-- (Not created by default to minimize overhead)
CREATE INDEX idx_audit_log_tx_lsn ON audit_log (__tx_lsn);

-- Use for efficient pagination
SELECT * FROM audit_log
WHERE __tx_lsn > 1000  -- Last seen counter
ORDER BY __tx_lsn
LIMIT 100;
```

#### Counter Recovery
```
Startup:
1. Read counters from $PGDATA/global/blockchain_counters
2. Initialize shared memory structures
3. Resume from last persisted value

Shutdown:
1. Write all counter values to disk
2. Ensure durability before exit

Crash Recovery:
1. Scan table for MAX(__tx_lsn)
2. Set counter = MAX + 1
3. Continue normal operation
```

#### Notes
- **Never Resets**: Counter never goes back to 1 (except manual intervention)
- **Capacity**: 2^63 rows per table (practically unlimited)
- **Persistence**: Written to disk every 100 operations and at shutdown
- **Ordering**: Use this column, not `__tx_timestamp`, for reliable ordering

---

### 6. `__tx_origin` - Transaction Origin

**PostgreSQL Type**: `UUID`
**OID**: `UUIDOID` (2950)
**Storage Size**: 16 bytes
**Nullability**: NOT NULL
**Generation**: Random UUID or configured origin identifier

#### Purpose
Identifies the source/origin of the transaction. Used for:
- Multi-site replication tracking
- Source system identification
- Distributed transaction correlation
- Cross-database audit trails

#### Example Values
```
123e4567-e89b-12d3-a456-426614174000  -- Application server 1
456e7890-e12b-34d5-a678-901234567890  -- Application server 2
789e0123-e45b-67d8-a901-234567890123  -- Batch import process
```

#### Usage Examples
```sql
-- Group by origin
SELECT __tx_origin, COUNT(*) AS transaction_count
FROM audit_log
GROUP BY __tx_origin;

-- Filter by specific origin
SELECT * FROM audit_log
WHERE __tx_origin = '123e4567-e89b-12d3-a456-426614174000';

-- Track multi-site data
SELECT __tx_origin,
       MIN(__tx_timestamp) AS first_transaction,
       MAX(__tx_timestamp) AS last_transaction,
       COUNT(*) AS total_transactions
FROM audit_log
GROUP BY __tx_origin;
```

#### Configuration
```sql
-- Set origin for session (future feature)
SET blockchain.tx_origin = '123e4567-e89b-12d3-a456-426614174000';

-- Or use application_name mapping
SET application_name = 'app_server_1';
-- System maps to predefined UUID
```

#### Notes
- **Default**: Random UUID if not configured
- **Distributed Systems**: Enables tracking across multiple databases
- **Replication**: Preserves origin information across replicas
- **Future**: Configuration options for custom origin assignment

---

### 7. `__tx_version` - Version Number

**PostgreSQL Type**: `INTEGER` (INT4)
**OID**: `INT4OID` (23)
**Storage Size**: 4 bytes
**Nullability**: NOT NULL
**Generation**: Always 1 in current implementation
**Future Range**: 1 to 2,147,483,647

#### Purpose
Supports multi-version concurrency control for logical updates. Used for:
- Row versioning (when logical updates implemented)
- Historical data tracking
- Temporal queries
- Audit compliance

#### Current Implementation
```sql
-- Always set to 1 (no versions yet)
__tx_version = 1
```

#### Future Implementation (Logical Updates)
```sql
-- Version 1 (original)
INSERT INTO audit_log (user_id, action) VALUES (1001, 'login');
-- __tx_version = 1, __is_latest = true

-- Version 2 (logical update)
-- System creates new row:
-- __tx_version = 2, __is_latest = true
-- Original row: __is_latest = false
```

#### Example Values
```
1  -- First version (current)
2  -- Second version (after logical update)
3  -- Third version
...
```

#### Usage Examples (Future)
```sql
-- Get latest version of each row
SELECT * FROM audit_log WHERE __is_latest = true;

-- Get specific version
SELECT * FROM audit_log
WHERE __row_id = '550e8400-e29b-41d4-a716-446655440000'
  AND __tx_version = 2;

-- Get all versions of a row
SELECT __tx_version, __tx_timestamp, user_id, action
FROM audit_log
WHERE __row_id = '550e8400-e29b-41d4-a716-446655440000'
ORDER BY __tx_version;

-- Count versions per row
SELECT __row_id, COUNT(*) AS version_count
FROM audit_log
GROUP BY __row_id
HAVING COUNT(*) > 1;
```

#### Notes
- **Current**: Always 1 (no versioning yet)
- **Future**: Will increment for logical updates
- **Immutability**: Each version is immutable
- **Performance**: Index on `(__row_id, __tx_version)` for versioning queries

---

### 8. `__is_latest` - Latest Version Flag

**PostgreSQL Type**: `BOOLEAN` (BOOL)
**OID**: `BOOLOID` (16)
**Storage Size**: 1 byte
**Nullability**: NOT NULL
**Generation**: Always TRUE in current implementation

#### Purpose
Identifies the current/latest version of a row for logical update scenarios. Used for:
- Fast filtering to latest data
- Version management
- Historical vs current data separation
- Query optimization

#### Current Implementation
```sql
-- Always set to true (no versions yet)
__is_latest = true
```

#### Future Implementation (Logical Updates)
```sql
-- After logical update:
-- Old version: __is_latest = false
-- New version: __is_latest = true
```

#### Example Values
```
true   -- Current version
false  -- Historical version (after logical update)
```

#### Usage Examples
```sql
-- Get only latest/current data (fastest query)
SELECT * FROM audit_log WHERE __is_latest = true;

-- Get only historical versions
SELECT * FROM audit_log WHERE __is_latest = false;

-- Point-in-time query (future feature)
SELECT * FROM audit_log
WHERE __is_latest = true
   OR __tx_timestamp > '2025-08-01 00:00:00';
```

#### Performance Optimization
```sql
-- Partial index for latest versions only
CREATE INDEX idx_audit_log_latest
ON audit_log (user_id, __tx_timestamp)
WHERE __is_latest = true;

-- This index is much smaller and faster for current data queries
```

#### Notes
- **Current**: Always TRUE (all rows are latest)
- **Future**: Will be FALSE for superseded versions
- **Indexing**: Consider partial index on `WHERE __is_latest = true`
- **Performance**: Bitmap scan optimization for this column

---

### 9. `__tx_timestamp` - Transaction Timestamp

**PostgreSQL Type**: `TIMESTAMP WITH TIME ZONE` (TIMESTAMPTZ)
**OID**: `TIMESTAMPTZOID` (1184)
**Storage Size**: 8 bytes
**Nullability**: NOT NULL
**Generation**: Set to current transaction timestamp at insert time
**Resolution**: Microsecond precision
**Range**: 4713 BC to 294276 AD

#### Purpose
Records the exact time when the row was inserted. Used for:
- Audit trails and compliance
- Time-based queries and analysis
- Data retention policies
- Forensic investigation
- Temporal analytics

#### Timestamp Characteristics
- **Transaction Time**: All rows in same transaction have same timestamp
- **UTC Aware**: Stores as UTC, displays in session timezone
- **Precision**: Microsecond (6 decimal places)
- **Source**: PostgreSQL transaction timestamp (not system clock)
- **Immutable**: Cannot be changed after insertion

#### Example Values
```
2025-08-12 10:30:00.123456+00
2025-08-12 14:25:30.987654+00
2025-10-31 18:45:12.456789+00
```

#### Usage Examples
```sql
-- Get rows from specific date
SELECT * FROM audit_log
WHERE __tx_timestamp::date = '2025-08-12';

-- Get rows in time range
SELECT * FROM audit_log
WHERE __tx_timestamp BETWEEN '2025-08-01' AND '2025-08-31';

-- Get recent rows (last 24 hours)
SELECT * FROM audit_log
WHERE __tx_timestamp > NOW() - INTERVAL '24 hours'
ORDER BY __tx_timestamp DESC;

-- Hourly activity report
SELECT date_trunc('hour', __tx_timestamp) AS hour,
       COUNT(*) AS transaction_count
FROM audit_log
GROUP BY hour
ORDER BY hour DESC;

-- Timezone conversion
SELECT __tx_timestamp AT TIME ZONE 'America/New_York' AS eastern_time,
       __tx_timestamp AT TIME ZONE 'Europe/London' AS london_time
FROM audit_log;
```

#### Performance Considerations
```sql
-- Index for time-based queries (optional)
CREATE INDEX idx_audit_log_timestamp ON audit_log (__tx_timestamp);

-- Or BRIN index for large, append-only tables (more efficient)
CREATE INDEX idx_audit_log_timestamp_brin
ON audit_log USING BRIN (__tx_timestamp);
```

#### Data Retention Queries
```sql
-- Find old data (for archiving)
SELECT COUNT(*) FROM audit_log
WHERE __tx_timestamp < NOW() - INTERVAL '7 years';

-- Partition by timestamp (future feature)
-- CREATE TABLE audit_log_2025_08 PARTITION OF audit_log
-- FOR VALUES FROM ('2025-08-01') TO ('2025-09-01');
```

#### Notes
- **Not Ordering Key**: Use `__tx_lsn` for reliable ordering, not timestamp
- **Same for Transaction**: Multiple rows inserted together have same timestamp
- **Timezone**: Store times consistently, convert for display
- **Indexing**: BRIN index recommended for large tables

---

## System Column Visibility

### Default Behavior
```sql
-- System columns are HIDDEN by default
SELECT * FROM audit_log;
-- Returns only user-defined columns: user_id, action, ip_address, etc.
```

### Explicit Access
```sql
-- Access specific system column
SELECT user_id, action, __tx_lsn, __tx_timestamp
FROM audit_log;

-- Access all system columns
SELECT __row_id, __curr_hash, __prev_hash, __tx_type, __tx_lsn,
       __tx_origin, __tx_version, __is_latest, __tx_timestamp,
       *  -- User columns
FROM audit_log;

-- Use in WHERE clause
SELECT * FROM audit_log
WHERE __tx_lsn > 1000
  AND __tx_timestamp > '2025-08-01';

-- Use in ORDER BY
SELECT * FROM audit_log
ORDER BY __tx_lsn DESC;
```

### Wildcard Expansion
```sql
-- SELECT * excludes system columns
SELECT * FROM audit_log;  -- Only user columns

-- SELECT t.* also excludes system columns
SELECT t.* FROM audit_log t;  -- Only user columns

-- Explicitly name columns to include system columns
SELECT __tx_lsn, __tx_timestamp, * FROM audit_log;
```

---

## Storage Overhead Analysis

### Per-Row Overhead Breakdown
```
Fixed-size columns:
  __row_id        16 bytes  (UUID)
  __tx_lsn         8 bytes  (BIGINT)
  __tx_origin     16 bytes  (UUID)
  __tx_version     4 bytes  (INTEGER)
  __is_latest      1 byte   (BOOLEAN)
  __tx_timestamp   8 bytes  (TIMESTAMPTZ)
  Subtotal:       53 bytes

Variable-size columns:
  __curr_hash     36 bytes  (32 + 4 VARLENA header)
  __prev_hash     36 bytes  (32 + 4 VARLENA header)
  __tx_type       ~10 bytes (6 chars + 4 VARLENA header)
  Subtotal:       82 bytes

Tuple header overhead:
  Heap tuple header: 23 bytes
  Alignment padding: ~8 bytes

Total per-row overhead: ~166 bytes
With alignment and overhead: 200-250 bytes
```

### Table Size Impact
```sql
-- Calculate storage overhead
SELECT
  pg_size_pretty(pg_total_relation_size('audit_log')) AS total_size,
  COUNT(*) AS row_count,
  pg_size_pretty(pg_total_relation_size('audit_log') / NULLIF(COUNT(*), 0)) AS avg_row_size,
  pg_size_pretty(200 * COUNT(*)) AS estimated_system_column_overhead
FROM audit_log;
```

### Example Calculation
```
Table: 1,000,000 rows
User data per row: ~100 bytes
System columns: ~200 bytes
Total per row: ~300 bytes

Total size:
  User data: 100 MB
  System columns: 200 MB
  Indexes + TOAST: 50 MB
  Total: 350 MB

Overhead percentage: 200/300 = 67% overhead
```

### Optimization Tips
```sql
-- System columns are NOT indexed by default
-- Create indexes only when needed:

-- For counter-based queries
CREATE INDEX idx_audit_log_lsn ON audit_log (__tx_lsn);

-- For time-based queries (BRIN is smaller)
CREATE INDEX idx_audit_log_ts ON audit_log USING BRIN (__tx_timestamp);

-- For row lookup
CREATE INDEX idx_audit_log_rowid ON audit_log (__row_id);
```

---

## Query Performance Best Practices

### Fast Queries (Use These)
```sql
-- Efficient: Query by counter (sequential scan or index)
SELECT * FROM audit_log WHERE __tx_lsn BETWEEN 1000 AND 2000;

-- Efficient: Query by timestamp with index
SELECT * FROM audit_log WHERE __tx_timestamp > NOW() - INTERVAL '1 day';

-- Efficient: Query by row_id with index
SELECT * FROM audit_log WHERE __row_id = '550e8400-e29b-41d4-a716-446655440000';
```

### Slow Queries (Avoid These)
```sql
-- Inefficient: Hash comparison (sequential scan of large bytea columns)
SELECT * FROM audit_log WHERE __curr_hash = '\x1a2b3c...'::bytea;

-- Inefficient: String operations on hashes
SELECT * FROM audit_log WHERE encode(__curr_hash, 'hex') LIKE '%1a2b3c%';

-- Inefficient: Unindexed text search
SELECT * FROM audit_log WHERE __tx_type LIKE 'INS%';
```

### Index Recommendations
```sql
-- For OLTP workloads (frequent lookups)
CREATE INDEX idx_audit_log_lsn ON audit_log (__tx_lsn);
CREATE INDEX idx_audit_log_rowid ON audit_log (__row_id);

-- For OLAP workloads (time-series analysis)
CREATE INDEX idx_audit_log_ts_brin ON audit_log USING BRIN (__tx_timestamp);

-- For filtered queries (latest versions only)
CREATE INDEX idx_audit_log_latest ON audit_log (user_id)
WHERE __is_latest = true;
```

---

## Common Use Cases

### Audit Trail Analysis
```sql
SELECT __tx_lsn, __tx_timestamp, __tx_type, user_id, action
FROM audit_log
WHERE __tx_timestamp > NOW() - INTERVAL '30 days'
ORDER BY __tx_lsn DESC;
```

### Hash Chain Export
```sql
SELECT __tx_lsn AS counter,
       encode(__curr_hash, 'hex') AS current_hash,
       encode(__prev_hash, 'hex') AS previous_hash,
       __tx_timestamp AS timestamp
FROM audit_log
ORDER BY __tx_lsn;
```

### Data Forensics
```sql
SELECT __row_id, __tx_lsn, __tx_timestamp, __tx_origin,
       encode(__curr_hash, 'hex') AS data_fingerprint
FROM audit_log
WHERE user_id = 1001
ORDER BY __tx_timestamp;
```

### Compliance Reporting
```sql
SELECT
  __tx_timestamp::date AS date,
  COUNT(*) AS total_transactions,
  COUNT(DISTINCT user_id) AS unique_users,
  MIN(__tx_lsn) AS first_counter,
  MAX(__tx_lsn) AS last_counter
FROM audit_log
WHERE __tx_timestamp BETWEEN '2025-01-01' AND '2025-12-31'
GROUP BY date
ORDER BY date;
```

---

## Error Handling

### Common Errors

#### Attempting to Modify System Columns
```sql
-- ERROR: cannot modify system column "__tx_lsn"
UPDATE audit_log SET __tx_lsn = 5000 WHERE user_id = 1001;

-- ERROR: cannot insert into system column "__curr_hash"
INSERT INTO audit_log (__curr_hash, user_id) VALUES ('\x1a2b...'::bytea, 1001);
```

#### Invalid System Column References
```sql
-- ERROR: column "__invalid_column" does not exist
SELECT __invalid_column FROM audit_log;
```

#### Type Mismatches
```sql
-- ERROR: operator does not exist: bigint = text
SELECT * FROM audit_log WHERE __tx_lsn = 'abc';

-- Fix: Use correct type
SELECT * FROM audit_log WHERE __tx_lsn = 1000;
```

---

## Summary

### Key Takeaways

1. **Automatic**: All 9 system columns are automatically added and managed
2. **Hidden**: System columns are hidden by default from `SELECT *`
3. **Immutable**: System columns cannot be modified after row creation
4. **Overhead**: ~200-250 bytes per row storage overhead
5. **No Indexes**: System columns are not indexed by default
6. **Performance**: Use `__tx_lsn` for ordering, not `__tx_timestamp`
7. **Security**: Hash chain provides cryptographic tamper-evidence
8. **Audit**: Complete audit trail with timestamps, counters, and hashes

### Quick Reference
```sql
-- View all system columns
SELECT __row_id, __curr_hash, __prev_hash, __tx_type, __tx_lsn,
       __tx_origin, __tx_version, __is_latest, __tx_timestamp
FROM your_blockchain_table
LIMIT 10;

-- Essential columns for most use cases
SELECT __tx_lsn, __tx_timestamp, __row_id, *
FROM your_blockchain_table
ORDER BY __tx_lsn DESC;
```

---

**Related Documentation**:
- [SQL Functions Reference](sql-functions-reference.md) - Query and verification functions
- [C API Reference](c-api-reference.md) - System column implementation details
- [Configuration](configuration.md) - Performance tuning for system columns
