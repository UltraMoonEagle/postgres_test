# Configuration Guide

This guide covers configuration options for the PostgreSQL Blockchain Extension, including code-level constants, runtime parameters, and performance tuning recommendations.

**Related documentation:**

- [PostgreSQL Configuration](pg-configuration.md) - PostgreSQL server settings
- [Shared Memory Architecture](../architecture/shared-memory.md) - Memory management details
- [Performance Benchmarks](../testing/performance.md) - Performance characteristics

---

## Quick Start Configuration

For production deployments with moderate workloads (4–16 concurrent clients):

**Code constants** (requires rebuild):

```c
// src/include/blockchain/blockchain_counter.h
#define MAX_BLOCKCHAIN_TABLES 1024
#define MAX_CACHED_HASHES 50000

// src/backend/access/blockchain/blockchain_phantom_optimized.c
#define MAX_PHANTOM_BLOCKS_PER_TXN 50000
```

**PostgreSQL settings:**

```ini
# postgresql.conf
shared_buffers = 512MB
max_connections = 100
shared_preload_libraries = 'blockchain_anchor'
```

After changing code constants:

```bash
make clean && make -j4 && make install
pg_ctl restart -D ~/pgsql/data
```

---

## Code-Level Configuration

These constants are defined at compile time and require rebuilding the extension to modify.

### Hash Cache Size

**File:** `src/include/blockchain/blockchain_counter.h`

```c
/* Maximum number of uncommitted hashes to cache */
#define MAX_CACHED_HASHES 50000
```

**Purpose:** Controls the size of the shared memory hash cache that stores uncommitted transaction hashes for cross-transaction visibility.

**Memory impact:** ~82 bytes per entry

| Setting | Memory | Suitable for |
|---------|--------|--------------|
| 20,000 | ~1.58 MB | Light workloads (1–4 clients) |
| **50,000** | **~3.94 MB** | **Production default (4–16 clients)** |
| 100,000 | ~7.88 MB | Heavy workloads (16+ clients) |
| 200,000 | ~15.76 MB | Very heavy workloads (32+ clients) |

**Automatic cleanup:**

- Triggers at 90% capacity
- Removes 50% of entries when triggered
- Enables unlimited transaction sizes

**When to increase:**

- Frequent cleanup log messages (every few seconds)
- Many concurrent long-running transactions
- High-throughput continuous insert workloads

**When to decrease:**

- Memory-constrained environments
- Primarily short transactions (<1000 rows)
- Single-client workloads

### Phantom Block Limit

**File:** `src/backend/access/blockchain/blockchain_phantom_optimized.c`

```c
/* Maximum phantom blocks per transaction */
#define MAX_PHANTOM_BLOCKS_PER_TXN 50000
```

**Purpose:** Limits the number of phantom blocks (rolled-back transaction records) tracked per transaction to prevent excessive logging.

**Impact:** Cosmetic only - prevents warning messages in logs. Phantom blocks are still tracked correctly even if limit exceeded.

**Default:** 50,000 (matches hash cache size)

**When to increase:**

- Transactions routinely rollback >50k rows
- Warning messages cluttering logs

**Memory impact:** Minimal - phantom blocks stored in regular table, not shared memory.

### Maximum Blockchain Tables

**File:** `src/include/blockchain/blockchain_counter.h`

```c
/* Maximum number of blockchain-enabled tables */
#define MAX_BLOCKCHAIN_TABLES 1024
```

**Purpose:** Maximum number of blockchain tables that can exist in the database.

**Memory impact:** ~82 bytes per table (~82 KB for 1024 tables)

**When to change:** Rarely needed. Increase only if you need more than 1,024 blockchain tables in a single database.

---

## Memory Configuration

### Production Configuration Matrix

Total system memory determines safe configuration limits:

| System RAM | shared_buffers | MAX_CACHED_HASHES | Max Connections | Concurrent Clients |
|-----------|----------------|-------------------|-----------------|-------------------|
| 2 GB | 256–512 MB | 20,000–50,000 | 50 | 16 |
| 4 GB | 512 MB–1 GB | 50,000 | 100 | 32 |
| 8 GB | 1–2 GB | 100,000 | 200 | 64+ |
| 16+ GB | 2–4 GB | 200,000 | 400 | 128+ |

**Memory calculation formula:**

```
Total = shared_buffers 
      + (MAX_CACHED_HASHES × 82 bytes)
      + (max_connections × 10 MB)
      + 50 MB PostgreSQL overhead

Example (production default):
  512 MB  (shared_buffers)
+ 3.94 MB (50,000 hash entries)
+ 1000 MB (100 connections × 10MB)
+ 50 MB   (PostgreSQL overhead)
= 1,566 MB total
```

**Rule of thumb:** Use ~40% of total RAM for PostgreSQL.

---

## Performance Tuning by Workload

### Real-Time Audit Logging

**Characteristics:**

- 10–32 concurrent clients
- 1-row transactions (autocommit mode)
- Low latency requirement
- Expected throughput: 625 TPS per client

**Configuration:**

```c
// Code
#define MAX_CACHED_HASHES 50000
```

```ini
# postgresql.conf
shared_buffers = 512MB
effective_cache_size = 1GB
work_mem = 4MB
max_connections = 100
```

**Application pattern:**

```sql
-- Each INSERT is separate transaction
INSERT INTO audit_log VALUES (...);
INSERT INTO audit_log VALUES (...);
```

### ETL / Bulk Import

**Characteristics:**

- 1–4 concurrent ETL processes
- 10k–50k rows per transaction
- High throughput requirement
- Expected throughput: 15,000–20,000 rows/sec

**Configuration:**

```c
// Code
#define MAX_CACHED_HASHES 100000  // Larger for long transactions
```

```ini
# postgresql.conf
shared_buffers = 1GB
effective_cache_size = 2GB
work_mem = 128MB  # Higher for large sorts
maintenance_work_mem = 1GB
max_connections = 50
checkpoint_timeout = 15min
```

**Application pattern:**

```sql
-- Batch operations
BEGIN;
INSERT INTO data_warehouse 
SELECT * FROM staging_table;  -- 50k rows
COMMIT;
```

### High-Concurrency Web Application

**Characteristics:**

- 32–100+ concurrent clients
- Mixed transaction sizes (100–5k rows)
- Connection pooling recommended
- Expected throughput: 10,000+ TPS system-wide

**Configuration:**

```c
// Code
#define MAX_CACHED_HASHES 200000  // Very large cache
```

```ini
# postgresql.conf
shared_buffers = 2GB
effective_cache_size = 4GB
work_mem = 16MB
max_connections = 200
```

**Connection pooling (pgBouncer):**

```ini
# pgbouncer.ini
[databases]
mydb = host=localhost port=5432 dbname=mydb

[pgbouncer]
pool_mode = transaction
max_client_conn = 1000      # Application connections
default_pool_size = 200     # PostgreSQL connections
```

---

## Runtime Configuration

### Extension Loading

The extension must be preloaded at server start to allocate shared memory:

```ini
# postgresql.conf
shared_preload_libraries = 'blockchain_anchor'
```

**Critical:** Server restart required after changing this setting.

**Multiple extensions:**

```ini
shared_preload_libraries = 'blockchain_anchor,pg_stat_statements'
```

**Verification:**

```sql
-- Check extension loaded
SELECT * FROM pg_extension WHERE extname = 'blockchain_anchor';

-- Check version
SELECT extversion FROM pg_extension WHERE extname = 'blockchain_anchor';
```

### Extension Installation

After building and server restart:

```sql
-- Create extension in target database
CREATE EXTENSION IF NOT EXISTS blockchain_anchor;

-- Verify system functions available
\df blockchain_*

-- Verify helper functions
\df is_blockchain_table
\df verify_hash_chain
```

### GUC Parameters

The extension provides runtime configuration parameters (GUCs - Grand Unified Configuration) to control blockchain behavior.

#### blockchain.use_atomic_counters

**Description:** Enable lock-free atomic counter operations for improved concurrent performance.

**Type:** `boolean`
**Context:** `POSTMASTER` (requires server restart)
**Default:** `true`
**Range:** `on` | `off`

**Impact:**
- **ON**: Uses lock-free `pg_atomic_uint64` operations (6-10× faster at 16+ clients)
- **OFF**: Falls back to traditional LWLock implementation

**Configuration:**

```ini
# postgresql.conf
blockchain.use_atomic_counters = on  # Recommended for production
```

```sql
-- View current setting
SHOW blockchain.use_atomic_counters;

-- Change setting (requires restart)
ALTER SYSTEM SET blockchain.use_atomic_counters = on;
SELECT pg_reload_conf();
-- THEN: pg_ctl restart
```

**When to disable:**
- Platform doesn't support 64-bit atomics (very rare)
- Debugging counter-related issues
- Benchmarking LWLock vs atomic performance

---

#### blockchain.hash_cache_partitions

**Description:** Number of partitions for the hash cache to reduce lock contention.

**Type:** `integer`
**Context:** `POSTMASTER` (requires server restart)
**Default:** `64`
**Range:** `1` to `256`

**Impact:**
- Higher values reduce contention at high concurrency
- Lower values reduce memory overhead
- Must be power of 2 for optimal hash distribution

**Configuration:**

```ini
# postgresql.conf
blockchain.hash_cache_partitions = 64  # Default
blockchain.hash_cache_partitions = 128  # High concurrency workloads
```

**Recommendation by workload:**

| Concurrent Clients | Partitions | Memory Overhead |
|-------------------|------------|-----------------|
| 1-4 | 16 | Minimal |
| 4-16 | 64 | ~8 KB |
| 16-32 | 128 | ~16 KB |
| 32+ | 256 | ~32 KB |

---

#### blockchain.retry_initial_us

**Description:** Initial retry delay when waiting for previous transaction's hash (microseconds).

**Type:** `integer`
**Context:** `USERSET` (immediate effect)
**Default:** `100` (0.1 milliseconds)
**Range:** `10` to `100000` (0.01ms to 100ms)

**Impact:**
- Lower values: More CPU usage, faster detection
- Higher values: Less CPU usage, higher latency on hash lookup

**Configuration:**

```sql
-- Set for current session
SET blockchain.retry_initial_us = 50;

-- Set for specific user
ALTER USER myuser SET blockchain.retry_initial_us = 50;

-- Set globally
ALTER SYSTEM SET blockchain.retry_initial_us = 50;
SELECT pg_reload_conf();
```

**Tuning guide:**
- **High-frequency inserts (>1000 TPS):** Use `50-100` μs
- **Normal workloads:** Use `100` μs (default)
- **Low-frequency inserts:** Use `200-500` μs

---

#### blockchain.retry_max_us

**Description:** Maximum retry delay (exponential backoff cap) in microseconds.

**Type:** `integer`
**Context:** `USERSET` (immediate effect)
**Default:** `10000` (10 milliseconds)
**Range:** `100` to `1000000` (0.1ms to 1 second)

**Impact:**
- Controls maximum wait time between retry attempts
- Prevents excessive CPU spinning under contention

**Configuration:**

```sql
SET blockchain.retry_max_us = 5000;  -- 5ms max delay
```

**Relationship to initial delay:**
```
Retry delays: initial_us → 2×initial → 4×initial → ... → max_us (capped)
Example: 100 → 200 → 400 → 800 → 1600 → 3200 → 6400 → 10000 (max) → 10000 ...
```

---

#### blockchain.retry_max_total_ms

**Description:** Total timeout for hash lookup retries (milliseconds).

**Type:** `integer`
**Context:** `USERSET` (immediate effect)
**Default:** `1000` (1 second)
**Range:** `100` to `60000` (0.1s to 60s)

**Impact:**
- Controls how long to wait before giving up on hash lookup
- Too low: False failures in high-concurrency scenarios
- Too high: Long hangs if transactions deadlock

**Configuration:**

```sql
SET blockchain.retry_max_total_ms = 2000;  -- 2 second timeout
```

**Recommendation:**
- **Production:** `1000-2000` ms (1-2 seconds)
- **Development:** `500` ms (fail fast for debugging)
- **Stress testing:** `5000` ms (allow for heavy contention)

**Error when exceeded:**
```
ERROR:  Hash not found after 1000ms total retry time
HINT:   Previous transaction may have rolled back or system is overloaded
```

---

#### blockchain.batch_max_size

**Description:** Maximum batch size for batch insert operations.

**Type:** `integer`
**Context:** `USERSET` (immediate effect)
**Default:** `256`
**Range:** `1` to `10000`

**Impact:**
- Controls maximum rows per batch insert
- Higher values: Better throughput, more memory usage
- Lower values: Lower memory footprint, more overhead

**Configuration:**

```sql
SET blockchain.batch_max_size = 512;  -- Larger batches

-- For specific batch operation
SET LOCAL blockchain.batch_max_size = 1000;
INSERT INTO blockchain_table SELECT * FROM staging_table;  -- Uses 1000
-- Setting reverts after transaction
```

**Tuning by workload:**

| Workload Type | Batch Size | Reasoning |
|--------------|------------|-----------|
| Real-time OLTP | 100-256 | Low latency, frequent commits |
| ETL / Bulk load | 1000-5000 | Maximize throughput |
| Mixed | 256-512 | Balance |

---

### GUC Configuration Examples

**Production high-concurrency setup:**

```ini
# postgresql.conf
blockchain.use_atomic_counters = on
blockchain.hash_cache_partitions = 128
blockchain.retry_initial_us = 50
blockchain.retry_max_us = 10000
blockchain.retry_max_total_ms = 2000
blockchain.batch_max_size = 512
```

**Development/testing setup:**

```ini
# postgresql.conf
blockchain.use_atomic_counters = on
blockchain.hash_cache_partitions = 64
blockchain.retry_initial_us = 100
blockchain.retry_max_us = 5000
blockchain.retry_max_total_ms = 500  # Fail fast for debugging
blockchain.batch_max_size = 256
```

**Memory-constrained setup:**

```ini
# postgresql.conf
blockchain.use_atomic_counters = on
blockchain.hash_cache_partitions = 16  # Reduce overhead
blockchain.retry_initial_us = 200
blockchain.retry_max_us = 10000
blockchain.retry_max_total_ms = 1000
blockchain.batch_max_size = 128
```

**Viewing all blockchain GUCs:**

```sql
-- Show all blockchain parameters
SELECT name, setting, unit, context, short_desc
FROM pg_settings
WHERE name LIKE 'blockchain.%'
ORDER BY name;
```

**Sample output:**
```
              name                | setting | unit | context    |           short_desc
----------------------------------+---------+------+------------+----------------------------------
 blockchain.batch_max_size        | 256     |      | user       | Maximum batch insert size
 blockchain.hash_cache_partitions | 64      |      | postmaster | Number of hash cache partitions
 blockchain.retry_initial_us      | 100     | us   | user       | Initial retry delay
 blockchain.retry_max_total_ms    | 1000    | ms   | user       | Total retry timeout
 blockchain.retry_max_us          | 10000   | us   | user       | Maximum retry delay
 blockchain.use_atomic_counters   | on      |      | postmaster | Enable atomic counters
```

---

## Monitoring Configuration

### Enable Logging

```ini
# postgresql.conf
logging_collector = on
log_directory = 'log'
log_filename = 'postgresql-%Y-%m-%d_%H%M%S.log'
log_min_messages = log          # Captures hash cache cleanup
log_rotation_age = 1d
log_rotation_size = 100MB
```

### Monitor Hash Cache Cleanup

```bash
# Check for cleanup events
grep "Hash cache cleanup" ~/pgsql/data/log/postgresql-*.log | tail -20
```

**Expected output:**

```
2025-11-03 10:15:23 LOG:  Hash cache cleanup: 45001 entries (90.0% full), removing ~22500 entries
2025-11-03 10:15:23 LOG:  Hash cache cleanup: removed 22500 entries, 22501 remaining
```

**Interpreting cleanup frequency:**

| Frequency | Interpretation | Action |
|-----------|----------------|--------|
| Never | Normal (transactions <45k rows) | None |
| Every few minutes | Moderate throughput | Normal |
| Every few seconds | High throughput | Normal |
| Continuously | Cache thrashing | Increase `MAX_CACHED_HASHES` |

### Monitor Phantom Blocks

```sql
-- Check phantom block count
SELECT COUNT(*) FROM blockchain_phantom_blocks;

-- Check by table
SELECT 
    c.relname AS table_name,
    COUNT(*) AS phantom_count
FROM blockchain_phantom_blocks pb
JOIN pg_class c ON pb.table_oid = c.oid
GROUP BY c.relname
ORDER BY phantom_count DESC;
```

**Cleanup phantom blocks periodically:**

```sql
-- Safe to truncate after transactions commit
TRUNCATE blockchain_phantom_blocks;
```

---

## Troubleshooting Configuration

### Issue: Out of Shared Memory

**Symptom:**

```
ERROR:  out of shared memory
HINT:  You might need to increase max_locks_per_transaction
```

**Solution:**

1. Increase `shared_buffers` in postgresql.conf:

```ini
shared_buffers = 1GB  # Increase from 512MB
```

2. If insufficient, increase hash cache:

```c
#define MAX_CACHED_HASHES 100000  # Double from 50,000
```

3. Restart PostgreSQL after changes

### Issue: Excessive Cleanup Logs

**Symptom:** Many cleanup messages every few seconds

**Solution:** Increase hash cache size:

```c
#define MAX_CACHED_HASHES 100000  # Or higher
```

Rebuild extension:

```bash
make clean && make -j4 && make install
pg_ctl restart -D ~/pgsql/data
```

### Issue: Phantom Block Warnings

**Symptom:**

```
WARNING: Too many phantom blocks (50001 > 50000), some may not be tracked
```

**Impact:** Cosmetic only - phantom blocks still tracked correctly

**Solution:** Increase limit if warnings clutter logs:

```c
#define MAX_PHANTOM_BLOCKS_PER_TXN 100000
```

---

## Configuration Checklist

### Initial Setup

- [ ] Set `MAX_CACHED_HASHES` based on expected workload
- [ ] Configure `shared_buffers` in postgresql.conf
- [ ] Add `shared_preload_libraries = 'blockchain_anchor'`
- [ ] Build extension: `make clean && make -j4 && make install`
- [ ] Restart PostgreSQL
- [ ] Create extension: `CREATE EXTENSION blockchain_anchor;`

### Production Deployment

- [ ] Size shared_buffers based on total RAM (~25%)
- [ ] Configure max_connections for expected concurrency
- [ ] Enable comprehensive logging
- [ ] Set up phantom block cleanup schedule
- [ ] Configure connection pooling (if >100 clients)
- [ ] Test with realistic workload
- [ ] Monitor hash cache cleanup frequency
- [ ] Verify hash chain integrity: `SELECT * FROM verify_hash_chain('table');`

### Performance Tuning

- [ ] Profile with representative workload
- [ ] Monitor cache cleanup frequency
- [ ] Adjust `work_mem` for batch operations
- [ ] Consider increasing cache size if frequent cleanup
- [ ] Test concurrent client scaling

---

## Configuration Examples

### Example 1: Audit Logging System (10 clients)

```c
// Code configuration
#define MAX_CACHED_HASHES 50000
#define MAX_PHANTOM_BLOCKS_PER_TXN 50000
```

```ini
# postgresql.conf
shared_buffers = 512MB
max_connections = 100
shared_preload_libraries = 'blockchain_anchor'
synchronous_commit = on
```

**Expected performance:** 625 TPS per client = 6,250 TPS total

### Example 2: ETL System (4 parallel loads)

```c
// Code configuration
#define MAX_CACHED_HASHES 100000  # Larger for long transactions
```

```ini
# postgresql.conf
shared_buffers = 2GB
work_mem = 128MB
max_connections = 50
checkpoint_timeout = 15min
shared_preload_libraries = 'blockchain_anchor'
```

**Expected performance:** 15,000–20,000 rows/sec per process

### Example 3: Web Application (50 clients)

```c
// Code configuration
#define MAX_CACHED_HASHES 200000
```

```ini
# postgresql.conf
shared_buffers = 2GB
max_connections = 200
shared_preload_libraries = 'blockchain_anchor'

# Use pgBouncer for connection pooling
```

**Expected performance:** 10,000+ TPS system-wide

---

## Related Documentation

- [PostgreSQL Configuration](pg-configuration.md) - Server settings
- [Troubleshooting](troubleshooting.md) - Common issues
- [Performance Benchmarks](../testing/performance.md) - Performance data
- [Shared Memory Architecture](../architecture/shared-memory.md) - Memory details

---

For installation instructions, see [Installation Guide](installation.md).
