# Blockchain Stress Test Suite

Comprehensive stress tests to find system limits and breaking points.

## Test Suite Overview

### 1. Insert Volume Test (`01_insert_volume_test.sql`)
**Purpose:** Find maximum insert volume limits

**Tests:**
- 1,000 rows
- 10,000 rows
- 50,000 rows
- 100,000 rows

**Measures:**
- Insert performance degradation
- Hash chain verification at scale
- Table size growth
- Time per batch

### 2. Hash Cache Overflow Test (`02_hash_cache_overflow_test.sql`)
**Purpose:** Test behavior when uncommitted transactions exceed 10k cache limit

**Critical Test:** What happens when a single transaction inserts >10,000 rows?

**Tests:**
- 5,000 rows in one transaction (below limit)
- 15,000 rows in one transaction (EXCEEDS limit)
- 25,000 rows in one transaction
- Hash chain verification after overflow

**Expected Behavior:**
- If hash cache overflows, hashes should fall back to table lookup
- Hash chain should remain valid (0 broken links)

### 3. Rollback Stress Test (`03_rollback_stress_test.sql`)
**Purpose:** Test high abort rates and phantom block handling

**Tests:**
- 50% abort rate (100 commits, 100 rollbacks)
- 80% abort rate (100 commits, 400 rollbacks)
- 1,000 rapid rollbacks
- Phantom log file growth
- Hash chain with many phantom blocks

**Measures:**
- Phantom log file size
- Recovery with large phantom block count
- Hash chain integrity with mixed commits/aborts

### 4. Concurrent Clients Test (`04_concurrent_clients_test.sh`)
**Purpose:** Test scalability with multiple concurrent writers

**Configurations:**
- 4 clients × 1,000 rows = 4,000 total
- 8 clients × 1,000 rows = 8,000 total
- 16 clients × 500 rows = 8,000 total

**Measures:**
- Total throughput (rows/sec)
- Time per client
- Counter conflicts/duplicates (should be ZERO)
- Hash chain integrity with concurrent writes

**Critical Checks:**
- No duplicate counters
- No gaps in sequence
- Hash chain valid

### 5. Recovery Stress Test (`05_recovery_stress_test.sql` + `05b_verify_recovery.sql`)
**Purpose:** Test phantom block recovery with large log files

**Process:**
1. Create 2,000 commits + 1,500 phantom blocks
2. Verify log file size
3. **Manual step:** Restart PostgreSQL
4. Run verification script
5. Check recovery performance and correctness

**Measures:**
- Recovery time
- Phantom blocks recovered
- Hash chain integrity after recovery
- Log file truncation

## Running the Tests

### Run All Tests (Automated)
```bash
cd stress_tests
chmod +x run_all_stress_tests.sh
./run_all_stress_tests.sh
```

This will:
- Run all tests sequentially
- Generate comprehensive report
- Save results to timestamped file

### Run Individual Tests

**Insert Volume:**
```bash
psql -d postgres -f 01_insert_volume_test.sql
```

**Hash Cache Overflow:**
```bash
psql -d postgres -f 02_hash_cache_overflow_test.sql
```

**Rollback Stress:**
```bash
psql -d postgres -f 03_rollback_stress_test.sql
```

**Concurrent Clients:**
```bash
chmod +x 04_concurrent_clients_test.sh
./04_concurrent_clients_test.sh <num_clients> <rows_per_client>
# Example: ./04_concurrent_clients_test.sh 8 1000
```

**Recovery Test:**
```bash
# Step 1: Setup
psql -d postgres -f 05_recovery_stress_test.sql

# Step 2: Restart server
~/pgsql/bin/pg_ctl restart -D ~/pgsql/data -l ~/pgsql/data/logfile

# Step 3: Verify
psql -d postgres -f 05b_verify_recovery.sql
```

## Success Criteria

### All Tests Should Show:
- ✅ `broken_links = 0` (hash chain intact)
- ✅ `is_valid = true` for all entries
- ✅ No duplicate counters
- ✅ No gaps in chain (unless expected phantom blocks)

### Performance Expectations:
- **Insert Volume:** Should handle 100k+ rows without errors
- **Hash Cache:** Should NOT break chain even with >10k uncommitted
- **Rollbacks:** Should handle 1000+ phantom blocks
- **Concurrent:** Should scale linearly with client count
- **Recovery:** Should complete in <1 second for 1500 phantom blocks

## Critical Test: Hash Cache Overflow

**The most important test is #2 - Hash Cache Overflow.**

The hash cache is set to 10,000 entries. If a single transaction inserts >10k rows:
- What happens to the hash chain?
- Do hashes get lost?
- Does verification still work?

**Expected:** System should gracefully handle overflow by falling back to table lookups.

**If this fails:** We've found a critical limit!

## Analyzing Results

### Check for Broken Links
```sql
SELECT * FROM verify_hash_chain('your_test_table');
```
Should show `broken_links = 0`

### Check for Gaps
Look at counter sequences - any missing numbers are phantom blocks.

### Check Performance Degradation
Compare timing between early and late phases of insert volume test.

### Check Concurrent Write Conflicts
```sql
SELECT __tx_lsn, COUNT(*) as duplicates
FROM stress_test_concurrent
GROUP BY __tx_lsn
HAVING COUNT(*) > 1;
```
Should return 0 rows.

## Known Limits to Document

After running tests, document:
- Maximum single-transaction insert count
- Maximum concurrent clients before degradation
- Maximum phantom blocks before slow recovery
- Hash cache overflow behavior
- Recovery time scaling with phantom block count

## Extending Tests

To test larger scales:
- Modify row counts in test files
- Add more concurrent client configurations
- Test longer-running workloads
- Test sustained write rates over time
