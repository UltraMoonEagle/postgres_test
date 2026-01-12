# Phantom Blocks and Rollback Handling

## Overview

Phantom blocks are a critical feature that maintains hash chain integrity when transactions are rolled back. Without phantom blocks, rolled-back transactions would create gaps in the sequence counter, breaking the cryptographic hash chain.

**Key Concept:** Even though a transaction rolls back and its data is never committed, the blockchain system still allocated a counter and computed hashes. Phantom blocks preserve these hashes to maintain an unbroken chain.

---

## What Are Phantom Blocks?

When a transaction performs an INSERT into a blockchain table but then ROLLs BACK, the system:

1. **Allocates a counter** (e.g., counter 5)
2. **Computes hashes** (previous hash from counter 4 → current hash for counter 5)
3. **Rolls back** - data is NOT stored in the table
4. **Creates phantom block** - stores the hash in `blockchain_phantom_blocks` table

This allows the next transaction (counter 6) to use counter 5's hash as its previous hash, maintaining chain continuity.

---

## Why Phantom Blocks Matter

### Without Phantom Blocks (Broken Chain)

```
Counter 1 → [data] → hash: abc123
Counter 2 → [data] → hash: def456 (prev: abc123) ✓
Counter 3 → [ROLLBACK] → NO HASH STORED
Counter 4 → [data] → hash: ??? (prev: UNKNOWN) ✗ BROKEN!
```

**Result:** Hash chain breaks at counter 4 because counter 3's hash is missing.

### With Phantom Blocks (Intact Chain)

```
Counter 1 → [data] → hash: abc123
Counter 2 → [data] → hash: def456 (prev: abc123) ✓
Counter 3 → [PHANTOM] → hash: ghi789 (prev: def456) ✓ [stored in phantom_blocks]
Counter 4 → [data] → hash: jkl012 (prev: ghi789) ✓
```

**Result:** Hash chain remains intact. Counter 4 can retrieve counter 3's hash from the phantom blocks table.

---

## How Phantom Blocks Work

### Append-Only Log Architecture

Phantom blocks use an **append-only log file** for crash-safe persistence:

**Log File Location:** `$PGDATA/global/blockchain_phantom.log`

**Log Entry Format:** 84 bytes per transaction

```c
struct PhantomBlockLogEntry {
    Oid table_oid;                  // 4 bytes
    uint64 counter;                 // 8 bytes
    unsigned char prev_hash[32];    // 32 bytes
    unsigned char computed_hash[32]; // 32 bytes
    TransactionId xid;              // 4 bytes
    char status;                    // 1 byte: 'A'=allocated, 'C'=committed, 'R'=rolled back
    char padding[3];                // 3 bytes (alignment)
};
```

### Transaction Flow

**INSERT Operation:**

1. Allocate counter
2. Compute hashes
3. Write entry with status='A' (allocated) to log
4. Store hash in shared memory cache
5. Perform PostgreSQL INSERT

**COMMIT:**

- Append entry with status='C' to log
- Total I/O: 168 bytes sequential write (84 × 2)

**ROLLBACK:**

- Append entry with status='R' to log
- These entries become phantom blocks

### Automatic Recovery on Startup

When PostgreSQL starts, the first database connection triggers automatic recovery:

```
1. Read entire phantom log file
2. Build hash table tracking final status for each (table_oid, counter)
3. Create blockchain_phantom_blocks table (if doesn't exist)
4. INSERT entries with final status='R' into table
5. Truncate log file
```

**Recovery Log Example:**

```
LOG:  blockchain_recover_phantom_blocks: Starting recovery from log file
LOG:  blockchain_recover_phantom_blocks: Read 16 log entries
LOG:  blockchain_recover_phantom_blocks: Inserted 3 rolled-back phantom blocks
LOG:  blockchain_recover_phantom_blocks: Recovery complete
```

---

## Querying Phantom Blocks

### View Phantom Blocks for a Table

```sql
SELECT 
    table_oid::regclass AS table_name,
    counter,
    status,
    encode(prev_hash, 'hex') AS prev_hash,
    encode(computed_hash, 'hex') AS computed_hash,
    xid
FROM blockchain_phantom_blocks
WHERE table_oid = 'your_table'::regclass::oid
ORDER BY counter;
```

**Example Output:**

```
 table_name | counter | status |           prev_hash            |         computed_hash          |  xid
------------+---------+--------+--------------------------------+--------------------------------+-------
 audit_log  |       4 | ABORTED| def456789...                   | ghi789abc...                   | 12345
 audit_log  |       7 | ABORTED| jkl012345...                   | mno345678...                   | 12346
```

### Count Phantom Blocks

```sql
-- Total phantom blocks in database
SELECT COUNT(*) FROM blockchain_phantom_blocks;

-- Phantom blocks per table
SELECT 
    table_oid::regclass AS table_name,
    COUNT(*) AS phantom_count
FROM blockchain_phantom_blocks
GROUP BY table_oid
ORDER BY phantom_count DESC;
```

---

## Verification with Phantom Blocks

### Complete Hash Chain Verification

The `verify_hash_chain()` function automatically includes phantom blocks:

```sql
SELECT * FROM verify_hash_chain('your_table');
```

**Output:**

```
 total_rows | broken_links | chain_integrity         | details
-----------+--------------+-------------------------+----------------------------
         5 |            0 | PASS: Hash chain intact | All 4 hash links are valid
```

**Note:** `total_rows` counts only committed rows. Phantom blocks are checked internally but not included in the count.

### Export Complete Chain (Including Phantoms)

```sql
SELECT * FROM export_hash_chain('your_table');
```

**Example Output:**

```
 seq | chain_status | is_valid | curr_hash_hex | prev_hash_hex
-----+--------------+----------+---------------+---------------
   1 | GENESIS      | t        | abc123...     | 000000...
   2 | LINKED       | t        | def456...     | abc123...
   3 | LINKED       | t        | ghi789...     | def456...
   4 | PHANTOM      | t        | jkl012...     | ghi789...     ← Rolled back
   5 | LINKED       | t        | mno345...     | jkl012...
   6 | PHANTOM      | t        | pqr678...     | mno345...     ← Rolled back
   7 | PHANTOM      | t        | stu901...     | pqr678...     ← Rolled back
   8 | LINKED       | t        | vwx234...     | stu901...
```

**Interpretation:**

- **GENESIS**: First block (counter 1)
- **LINKED**: Committed transaction with valid hash link
- **PHANTOM**: Rolled-back transaction maintaining chain integrity
- **is_valid = t**: Hash link is cryptographically correct

**All rows show `is_valid = t` → complete chain integrity maintained!**

---

## Example: Transaction Rollback

### Scenario: Financial Transaction with Validation

```sql
-- Create table
CREATE BLOCKCHAIN TABLE financial_ledger (
    account_from TEXT NOT NULL,
    account_to TEXT NOT NULL,
    amount NUMERIC(15,2) NOT NULL,
    description TEXT
);

-- Insert successful transactions
INSERT INTO financial_ledger VALUES ('A', 'B', 100.00, 'Payment');
INSERT INTO financial_ledger VALUES ('B', 'C', 50.00, 'Transfer');

-- Attempt transaction that fails validation
BEGIN;
INSERT INTO financial_ledger VALUES ('C', 'D', 1000000.00, 'Large transfer');
-- Business logic detects suspicious amount
ROLLBACK;

-- Continue with normal transactions
INSERT INTO financial_ledger VALUES ('D', 'E', 75.00, 'Payment');
```

### Result

```sql
-- View committed data
SELECT __tx_lsn, account_from, account_to, amount FROM financial_ledger;
```

**Output:**

```
 __tx_lsn | account_from | account_to | amount
----------+--------------+------------+--------
        1 | A            | B          | 100.00
        2 | B            | C          |  50.00
        4 | D            | E          |  75.00
```

**Note:** Counter 3 is missing (rolled back).

```sql
-- View phantom blocks
SELECT counter, status FROM blockchain_phantom_blocks
WHERE table_oid = 'financial_ledger'::regclass::oid;
```

**Output:**

```
 counter | status
---------+---------
       3 | ABORTED
```

```sql
-- Verify chain integrity
SELECT * FROM verify_hash_chain('financial_ledger');
```

**Output:**

```
 total_rows | broken_links | chain_integrity
-----------+--------------+-----------------
          3 |            0 | PASS
```

**Hash chain is intact despite the rollback!**

---

## Performance Impact

### I/O Overhead

**Per Committed Transaction:**

- Append-only log: **168 bytes sequential I/O** (84 bytes × 2: allocated + committed)
- Sequential writes: ~500 MB/s on typical SSD
- **Overhead: ~1% compared to no phantom tracking**

**Per Rolled-Back Transaction:**

- Append-only log: **84 bytes sequential I/O** (1 entry: allocated)
- Minimal impact

### Comparison to Alternatives

| Approach | I/O per Transaction | Crash Safe | Overhead |
|----------|---------------------|------------|----------|
| **Append-only log** (current) | **168 bytes sequential** | ✅ Yes | **~1%** |
| Cache-only | 0 bytes | ❌ No | 0% (but unsafe) |
| Direct table writes | 2× full row size (random I/O) | ✅ Yes | 100-500%+ |
| WAL-based | 2× full row | ✅ Yes | 100%+ |

**Sequential I/O is 100× faster than random I/O**, making the append-only log approach highly efficient.

---

## Crash Safety

Phantom blocks are **fully crash-safe**:

1. **All writes are sequential** to append-only log
2. **Log survives server crashes**
3. **Automatic recovery on restart** loads phantom blocks from log
4. **No manual intervention required**

### Recovery Test

```bash
# Insert data with rollbacks
psql -c "INSERT INTO test VALUES (1); BEGIN; INSERT INTO test VALUES (2); ROLLBACK;"

# Simulate crash
pg_ctl stop -m immediate

# Restart
pg_ctl start

# Connect (triggers automatic recovery)
psql -c "SELECT COUNT(*) FROM blockchain_phantom_blocks;"
```

**Expected:** Phantom blocks are restored automatically.

---

## Maintenance

### Cleaning Up Old Phantom Blocks

Phantom blocks can be safely deleted after their associated transactions are well past:

```sql
-- Delete phantom blocks older than 30 days
DELETE FROM blockchain_phantom_blocks
WHERE xid < txid_current() - 1000000;  -- Approximate age-based cleanup

-- Or truncate all (safe after verification)
TRUNCATE blockchain_phantom_blocks;
```

**When is it safe to delete?**

- After hash chain verification passes
- When you no longer need forensic analysis of rolled-back transactions
- Periodically as part of maintenance (e.g., monthly)

**Important:** Deleting phantom blocks does NOT break hash chain integrity. The hashes are already used by committed transactions. Phantom blocks are only needed for:

- Historical verification
- Forensic analysis
- Audit trails showing all attempted operations

---

## Use Cases

### 1. Financial Audit Compliance

Track all attempted transactions (including rejected ones):

```sql
-- View all attempted operations
SELECT 
    counter,
    CASE 
        WHEN EXISTS (SELECT 1 FROM audit_log WHERE __tx_lsn = pb.counter) 
        THEN 'COMMITTED'
        ELSE 'REJECTED'
    END AS status,
    encode(computed_hash, 'hex') AS hash
FROM generate_series(1, (SELECT MAX(__tx_lsn) FROM audit_log)) AS counter
LEFT JOIN blockchain_phantom_blocks pb USING (counter);
```

### 2. Forensic Analysis

Reconstruct exact sequence of events:

```sql
-- Complete timeline with phantom blocks
WITH all_operations AS (
    SELECT __tx_lsn AS counter, 'COMMITTED' AS status, data
    FROM audit_log
    UNION ALL
    SELECT counter, 'ROLLED BACK' AS status, 'N/A' AS data
    FROM blockchain_phantom_blocks
    WHERE table_oid = 'audit_log'::regclass::oid
)
SELECT * FROM all_operations ORDER BY counter;
```

### 3. Regulatory Compliance (SOX, HIPAA)

Demonstrate complete immutable audit trail:

```sql
-- Prove hash chain integrity (including rollbacks)
SELECT * FROM export_hash_chain('compliance_log');
-- Show auditors: All entries valid, including rolled-back transactions
```

---

## Best Practices

1. **Don't disable phantom blocks** - They are essential for hash chain integrity

2. **Monitor phantom block count** - High counts may indicate application issues:
   ```sql
   SELECT COUNT(*) FROM blockchain_phantom_blocks;
   ```

3. **Periodic cleanup** - Delete old phantom blocks after verification:
   ```sql
   -- Monthly cleanup job
   DELETE FROM blockchain_phantom_blocks WHERE xid < txid_current() - 10000000;
   ```

4. **Include in backup/restore** - `blockchain_phantom_blocks` table must be backed up

5. **Verify after recovery** - After server restart, verify hash chain:
   ```sql
   SELECT * FROM verify_hash_chain('your_table');
   ```

---

## Related Documentation

- [Verification](verification.md) - Hash chain verification guide
- [System Columns](system-columns.md) - Understanding blockchain metadata
- [Architecture: Phantom Blocks](../architecture/phantom-blocks.md) - Technical implementation details
- [API Reference: Phantom Block Functions](../api/sql-functions-reference.md#phantom-blocks) - Function reference

---

For more details on the technical implementation, see [Architecture: Phantom Block System](../architecture/phantom-blocks.md).
