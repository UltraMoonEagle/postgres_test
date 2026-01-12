# Troubleshooting Guide

Common issues and solutions for the PostgreSQL Blockchain Extension. All issues documented here have been identified and resolved through comprehensive stress testing.

---

## Critical Issues (Resolved)

### Out of Shared Memory

**Status:** ✅ Resolved in production configuration

**Symptoms:**

```
ERROR:  out of shared memory
HINT:  You might need to increase max_locks_per_transaction
```

Occurs when:

- 8+ concurrent clients
- Transactions exceeding 45,000 rows
- Cumulative inserts across transactions exceed cache size

**Root Causes:**

1. Insufficient `shared_buffers` (default 128MB too small)
2. Small hash cache (previous 10,000 entry limit)
3. Missing automatic cleanup (cache filled with no eviction)

**Solution:**

**Step 1:** Increase shared_buffers

```ini
# postgresql.conf
shared_buffers = 512MB  # Minimum recommended
```

**Step 2:** Verify hash cache size

```c
// src/include/blockchain/blockchain_counter.h
#define MAX_CACHED_HASHES 50000  // Should be at least 50,000
```

**Step 3:** Verify automatic cleanup exists

```bash
grep "BlockchainCleanupHashCache" src/backend/access/blockchain/blockchain_counter.c
```

If function is missing, update to latest code version.

**Step 4:** Rebuild and restart

```bash
make clean && make -j4 && make install
pg_ctl restart -D ~/pgsql/data
```

**Step 5:** Verify fix

```sql
-- Test with large transaction
BEGIN;
INSERT INTO blockchain_table 
SELECT i, 'test' FROM generate_series(1, 60000) i;
COMMIT;

-- Verify hash chain integrity
SELECT * FROM verify_hash_chain('blockchain_table');
-- Expected: 0 broken links
```

**Check logs for cleanup:**

```bash
grep "Hash cache cleanup" ~/pgsql/data/log/postgresql-*.log
```

**Expected output:**

```
LOG:  Hash cache cleanup: 45001 entries (90.0% full), removing ~22500 entries
LOG:  Hash cache cleanup: removed 22500 entries, 22501 remaining
```

---

## Performance Issues

### Excessive Hash Cache Cleanup

**Symptom:** Many cleanup log messages every few seconds

```bash
grep "Hash cache cleanup" ~/pgsql/data/log/*.log | wc -l
# Output: 50+ in short time period
```

**Cause:** Hash cache size insufficient for workload

**Impact:** Minimal (<0.01% performance overhead), but indicates cache thrashing

**Solution:** Increase cache size

```c
// src/include/blockchain/blockchain_counter.h
#define MAX_CACHED_HASHES 100000  // Double the size
```

Rebuild:

```bash
make -j4 && make install
pg_ctl restart -D ~/pgsql/data
```

### Phantom Block Warnings

**Symptom:** Warnings during large transactions

```
WARNING: Too many phantom blocks (50001 > 50000), some may not be tracked
```

**Impact:** Cosmetic only - phantom blocks still tracked correctly

**Solution:** Increase limit if warnings clutter logs

```c
// src/backend/access/blockchain/blockchain_phantom_optimized.c
#define MAX_PHANTOM_BLOCKS_PER_TXN 100000
```

Rebuild required.

### Slow Performance (Expected)

**Symptom:**

- 10k row batch: 8.5 seconds (vs 56ms for regular table)
- 3× slower for single-row inserts

**Cause:** SHA-256 cryptographic hashing (0.55ms per row = 51% of overhead)

**This is expected behavior** - the cost of cryptographic verification.

**Overhead breakdown:**

| Component | Time per row | Percentage |
|-----------|-------------|-----------|
| SHA-256 hash | 0.55ms | 51% |
| Hash lookup | 0.10ms | 9% |
| Other blockchain ops | 0.43ms | 40% |
| **Total** | **1.08ms** | **100%** |

**Mitigation strategies:**

1. Use batch operations for higher throughput (15k–20k rows/sec)
2. Accept trade-off: Cryptographic guarantees require computation
3. Future: Hardware SHA acceleration (2–5× speedup)

**Not an issue to fix** - this is the fundamental cost of blockchain integrity.

---

## Database Connection Issues

### Extension Not Loaded

**Symptom:**

```
ERROR:  could not access file "blockchain_anchor": No such file or directory
```

**Cause:** Extension not in `shared_preload_libraries`

**Solution:**

```ini
# postgresql.conf
shared_preload_libraries = 'blockchain_anchor'
```

**Important:** Must restart (not reload)

```bash
pg_ctl restart -D ~/pgsql/data
```

**Verify:**

```sql
SELECT * FROM pg_extension WHERE extname = 'blockchain_anchor';
```

### Extension Functions Not Available

**Symptom:**

```sql
CREATE BLOCKCHAIN TABLE test (id INT);
-- ERROR: function is_blockchain_table does not exist
```

**Cause:** Extension created but not loaded properly (rare)

**Solution:**

```sql
-- Drop and recreate
DROP EXTENSION IF EXISTS blockchain_anchor CASCADE;
CREATE EXTENSION blockchain_anchor;

-- Verify
\dx blockchain_anchor
\df is_blockchain_table
```

---

## Hash Chain Integrity Issues

### Broken Hash Links

**Symptom:**

```sql
SELECT * FROM verify_hash_chain('my_table');
-- broken_links: 5
```

**This should NOT happen with current code.**

**Diagnosis:**

```sql
-- Find exact break points
WITH breaks AS (
    SELECT
        __tx_lsn,
        encode(__curr_hash, 'hex') AS curr,
        encode(__prev_hash, 'hex') AS prev,
        encode(LAG(__curr_hash) OVER (ORDER BY __tx_lsn), 'hex') AS expected_prev
    FROM my_table
)
SELECT * FROM breaks
WHERE __tx_lsn > 1 AND prev != expected_prev
LIMIT 10;
```

**Possible causes:**

1. Hash cache lookup failure (check logs)
2. Phantom block not tracked
3. Manual data modification (chain corrupted)

**Resolution:**

1. Check phantom blocks:

```sql
SELECT COUNT(*) FROM blockchain_phantom_blocks
WHERE table_oid = (SELECT oid FROM pg_class WHERE relname = 'my_table');
```

2. Check logs for errors:

```bash
grep -i error ~/pgsql/data/log/postgresql-*.log | grep -i blockchain
```

3. **If data manually modified:** Chain is corrupted and cannot be repaired.

### Missing Phantom Blocks

**Symptom:** Counter gaps but no phantom blocks found

```sql
-- Counters: 1, 2, 3, 5, 6 (gap at 4)
SELECT __tx_lsn FROM my_table ORDER BY __tx_lsn;

-- But no phantom block for counter 4
SELECT * FROM blockchain_phantom_blocks WHERE counter = 4;
-- (0 rows)
```

**Cause:** Phantom block cleaned up (expected behavior)

**Explanation:** Phantom blocks are temporary and safe to clean up after transactions commit. If hash chain is valid, this is correct behavior.

**Verification:**

```sql
-- Hash chain should be intact
SELECT * FROM verify_hash_chain('my_table');
-- Expected: 0 broken links
```

---

## Build and Installation Issues

### Permission Denied During Build

**Symptom:**

```bash
make -j4
# Permission denied: .deps/blockchainam.Po
```

**Solution:**

```bash
# Clean and rebuild
make clean
make -j4
make install
```

**If persists:**

```bash
# Check ownership
ls -la src/backend/access/blockchain/.deps/

# Fix ownership
sudo chown -R $USER:$USER src/backend/access/blockchain/
```

### Link Error: cannot find -lpq

**Symptom:**

```
/usr/bin/ld: cannot find -lpq
```

**Cause:** PostgreSQL development libraries not installed

**Solution:**

```bash
# Ubuntu/Debian
sudo apt-get install postgresql-server-dev-15

# RHEL/CentOS
sudo yum install postgresql-devel

# Or specify library path
export LD_LIBRARY_PATH=/usr/local/pgsql/lib:$LD_LIBRARY_PATH
```

---

## Diagnostics

### Quick Health Check

Run these commands to verify system health:

```bash
# 1. PostgreSQL running?
pg_isready
# Expected: accepting connections

# 2. Correct shared_buffers?
psql -c "SHOW shared_buffers;"
# Expected: 512MB or higher

# 3. Extension loaded?
psql -c "SELECT * FROM pg_extension WHERE extname = 'blockchain_anchor';"
# Expected: 1 row

# 4. Any OOM errors?
grep -i "out of.*memory" ~/pgsql/data/log/postgresql-*.log
# Expected: nothing (after fixes)

# 5. Check cache cleanup
grep "Hash cache cleanup" ~/pgsql/data/log/postgresql-*.log | tail -5
# Expected: cleanup logs for large transactions

# 6. Check phantom blocks
psql -c "SELECT COUNT(*) FROM blockchain_phantom_blocks;"
# Expected: count of rolled-back transactions
```

### Comprehensive Diagnostics

**Collect this information when reporting issues:**

1. **Versions:**

```bash
psql --version
psql -c "SELECT extversion FROM pg_extension WHERE extname = 'blockchain_anchor';"
```

2. **Configuration:**

```sql
SHOW shared_buffers;
SHOW max_connections;
SHOW synchronous_commit;  -- Must be 'on'
```

```bash
grep "MAX_CACHED_HASHES" src/include/blockchain/blockchain_counter.h
```

3. **Recent logs:**

```bash
tail -100 ~/pgsql/data/log/postgresql-*.log
```

4. **Error reproduction:**

```sql
-- Minimal example that reproduces the issue
```

---

## Quick Reference

| Issue | Quick Fix | Restart Required? |
|-------|-----------|------------------|
| OOM error | Increase `shared_buffers = 512MB` | Yes |
| Extension not found | Add `shared_preload_libraries` | Yes (restart) |
| Phantom warnings | Increase `MAX_PHANTOM_BLOCKS_PER_TXN` | Yes (rebuild) |
| Excessive cleanup | Increase `MAX_CACHED_HASHES` | Yes (rebuild) |
| Slow performance | Use batch transactions | No |
| Broken hash chain | Check phantom blocks and logs | N/A |
| Build permission error | `make clean && make` | N/A |

---

## Related Documentation

- [Configuration Guide](configuration.md) - Extension settings
- [PostgreSQL Configuration](pg-configuration.md) - Server settings
- [Performance Benchmarks](../testing/performance.md) - Expected performance
- [Shared Memory Architecture](../architecture/shared-memory.md) - Memory details

---

For installation instructions, see [Installation Guide](installation.md).
