# Configuration Reference - PostgreSQL Blockchain Extension

## Overview

This document provides comprehensive reference for all configuration parameters, compile-time constants, and runtime settings for the PostgreSQL Blockchain Extension. Proper configuration is essential for optimal performance, scalability, and reliability.

---

## Table of Contents

1. [Compile-Time Constants](#compile-time-constants)
2. [Runtime Parameters](#runtime-parameters)
3. [PostgreSQL Configuration](#postgresql-configuration)
4. [Memory Sizing Guidelines](#memory-sizing-guidelines)
5. [Performance Tuning](#performance-tuning)
6. [Production Recommendations](#production-recommendations)

---

## Compile-Time Constants

These constants are defined in header files and require recompilation to change.

### Core System Constants

| Constant | Value | Location | Description |
|----------|-------|----------|-------------|
| `NUM_BLOCKCHAIN_COLUMNS` | 9 | blockchainam.h:47 | Number of system columns per table |
| `MAX_BLOCKCHAIN_TABLES` | 1024 | blockchain_counter.h:24 | Maximum concurrent blockchain tables |
| `MAX_CACHED_HASHES` | 10000 | blockchain_counter.h:27 | Maximum uncommitted hashes in shared memory cache |
| `BLOCKCHAIN_TABLE_AM_OID` | 12336 | pg_am_d.h | OID of blockchain table access method |
| `RELKIND_BLOCKCHAIN_TABLE` | 'b' | pg_class_d.h | Relation kind identifier for blockchain tables |

### Hash Algorithm Constants

| Constant | Value | Location | Description |
|----------|-------|----------|-------------|
| `BC_SHA224_BLOCK_LENGTH` | 64 | blockchain_hash.h:64 | SHA-224 block size (bytes) |
| `BC_SHA224_DIGEST_LENGTH` | 28 | blockchain_hash.h:65 | SHA-224 output size (bytes) |
| `BC_SHA256_BLOCK_LENGTH` | 64 | blockchain_hash.h:66 | SHA-256 block size (bytes) |
| `BC_SHA256_DIGEST_LENGTH` | 32 | blockchain_hash.h:67 | SHA-256 output size (bytes) |
| `BC_SHA384_BLOCK_LENGTH` | 128 | blockchain_hash.h:68 | SHA-384 block size (bytes) |
| `BC_SHA384_DIGEST_LENGTH` | 48 | blockchain_hash.h:69 | SHA-384 output size (bytes) |
| `BC_SHA512_BLOCK_LENGTH` | 128 | blockchain_hash.h:70 | SHA-512 block size (bytes) |
| `BC_SHA512_DIGEST_LENGTH` | 64 | blockchain_hash.h:71 | SHA-512 output size (bytes) |

### Storage Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `BLOCKCHAIN_COUNTER_FILE` | "global/blockchain_counters" | Counter persistence file path |
| `BLOCKCHAIN_HASH_STORAGE_SIZE` | 36 | Total storage for hash (32 + 4 byte VARLENA header) |
| `COUNTER_PERSISTENCE_THRESHOLD` | 100 | Counter updates before disk sync |
| `COUNTER_FILE_VERSION` | 1 | Counter file format version |

### Modifying Compile-Time Constants

**Location**: `/home/eagle/Code/BC_Postgres/postgres_test/src/include/blockchain/`

#### Example: Increase Maximum Tables

```c
// File: blockchain_counter.h
// Line 24

// Default:
#define MAX_BLOCKCHAIN_TABLES 1024

// Increase to 4096:
#define MAX_BLOCKCHAIN_TABLES 4096
```

#### Example: Increase Hash Cache Size

```c
// File: blockchain_counter.h
// Line 27

// Default:
#define MAX_CACHED_HASHES 10000

// Increase to 50000:
#define MAX_CACHED_HASHES 50000
```

#### Recompilation Required
```bash
cd /home/eagle/Code/BC_Postgres/postgres_test
make clean
make -j$(nproc)
make install
```

---

## Runtime Parameters

### Current Runtime Parameters

Currently, the blockchain extension does not expose runtime GUC (Grand Unified Configuration) parameters. All behavior is controlled by compile-time constants.

### Future Runtime Parameters (Planned)

The following parameters are planned for future releases:

```ini
# postgresql.conf (future)

# Maximum number of blockchain tables (override compile-time default)
blockchain.max_tables = 1024

# Maximum cached hashes in shared memory
blockchain.max_cached_hashes = 10000

# Counter persistence interval (number of operations)
blockchain.counter_persistence_interval = 100

# Enable/disable hash chain verification on startup
blockchain.verify_on_startup = off

# Hash chain verification batch size
blockchain.verify_batch_size = 1000

# Enable detailed logging of blockchain operations
blockchain.debug_logging = off

# Transaction origin UUID (for distributed systems)
blockchain.tx_origin = 'auto'

# Hash algorithm selection (future multi-algorithm support)
blockchain.hash_algorithm = 'sha256'

# Automatic counter recovery on startup
blockchain.auto_recover_counters = on
```

### Setting Runtime Parameters (Future)

```sql
-- Session-level setting
SET blockchain.max_cached_hashes = 20000;

-- Database-level setting
ALTER DATABASE mydb SET blockchain.max_tables = 2048;

-- User-level setting
ALTER USER app_user SET blockchain.tx_origin = '123e4567-e89b-12d3-a456-426614174000';

-- View current settings
SHOW blockchain.max_tables;
SELECT name, setting, unit, context FROM pg_settings WHERE name LIKE 'blockchain.%';
```

---

## PostgreSQL Configuration

### Required PostgreSQL Settings

#### Shared Memory Configuration

The blockchain extension requires additional shared memory for counter management and hash caching.

```ini
# postgresql.conf

# Shared memory for blockchain extension
# Formula: (MAX_BLOCKCHAIN_TABLES * 128 bytes) + (MAX_CACHED_HASHES * 64 bytes)
# Default: (1024 * 128) + (10000 * 64) = 131,072 + 640,000 = 771,072 bytes ≈ 753 KB

# Increase shared_preload_libraries if using extension as library
# shared_preload_libraries = 'blockchain_extension'  # Future feature

# Ensure adequate shared memory
shared_buffers = 256MB  # Minimum recommended
```

#### Shared Memory Calculation

```
Base overhead per blockchain table:
  Counter entry: 128 bytes
  Total for 1024 tables: 131,072 bytes (128 KB)

Hash cache overhead:
  Hash entry: 64 bytes (32-byte hash + 32-byte key + metadata)
  Total for 10,000 entries: 640,000 bytes (625 KB)

Total shared memory requirement:
  Minimum: 128 KB + 625 KB = 753 KB
  Recommended: 2 MB (with headroom)
  Large installations (4096 tables, 50000 hashes): ~3.5 MB
```

#### Connection Settings

```ini
# postgresql.conf

# Ensure adequate connections for concurrent blockchain operations
max_connections = 100

# Work memory for hash computations (per connection)
work_mem = 4MB  # Minimum for complex queries

# Maintenance work memory for verification operations
maintenance_work_mem = 64MB
```

#### Write-Ahead Log (WAL) Settings

```ini
# postgresql.conf

# Blockchain tables generate INSERT-only workload
# Optimize WAL settings accordingly

# WAL level (logical recommended for replication)
wal_level = logical

# WAL buffers (increase for high-throughput INSERT workloads)
wal_buffers = 16MB

# Checkpoint settings (tune for INSERT-heavy workload)
checkpoint_timeout = 15min
max_wal_size = 2GB
min_wal_size = 1GB

# Full page writes (required for crash safety)
full_page_writes = on
```

#### Autovacuum Settings

**Important**: VACUUM is blocked on blockchain tables due to immutability. However, catalog tables and indexes still need autovacuum.

```ini
# postgresql.conf

# Enable autovacuum for catalog tables
autovacuum = on

# Aggressive settings for catalog tables
autovacuum_vacuum_scale_factor = 0.05
autovacuum_analyze_scale_factor = 0.02

# Note: Blockchain tables themselves do not require VACUUM
# System catalogs (pg_class, pg_attribute, etc.) still need it
```

---

## Memory Sizing Guidelines

### Shared Memory Sizing

#### Formula
```
Total Shared Memory = PostgreSQL Base + Blockchain Extension

Blockchain Extension Memory =
  (MAX_BLOCKCHAIN_TABLES × 128 bytes) +
  (MAX_CACHED_HASHES × 64 bytes)
```

#### Sizing Examples

**Small Installation** (10-50 blockchain tables, low concurrency)
```ini
MAX_BLOCKCHAIN_TABLES = 256
MAX_CACHED_HASHES = 2000

Calculation:
  Tables: 256 × 128 = 32,768 bytes (32 KB)
  Hashes: 2000 × 64 = 128,000 bytes (125 KB)
  Total: 157 KB

shared_buffers = 128MB
work_mem = 4MB
```

**Medium Installation** (100-500 blockchain tables, moderate concurrency)
```ini
MAX_BLOCKCHAIN_TABLES = 1024  # Default
MAX_CACHED_HASHES = 10000     # Default

Calculation:
  Tables: 1024 × 128 = 131,072 bytes (128 KB)
  Hashes: 10000 × 64 = 640,000 bytes (625 KB)
  Total: 753 KB

shared_buffers = 512MB
work_mem = 8MB
```

**Large Installation** (500+ blockchain tables, high concurrency)
```ini
MAX_BLOCKCHAIN_TABLES = 4096
MAX_CACHED_HASHES = 50000

Calculation:
  Tables: 4096 × 128 = 524,288 bytes (512 KB)
  Hashes: 50000 × 64 = 3,200,000 bytes (3.05 MB)
  Total: 3.56 MB

shared_buffers = 2GB
work_mem = 16MB
```

### Per-Connection Memory

```
Per-connection overhead for blockchain operations:
  Hash computation context: ~4-8 KB (temporary)
  Tuple slot buffers: ~8-16 KB (temporary)
  SPI memory context: ~16-32 KB (for query anchoring)

Total per-connection: ~32-64 KB (temporary, cleaned up after transaction)
```

### Disk Space Requirements

#### Counter Persistence File
```
File: $PGDATA/global/blockchain_counters
Size: (number_of_tables × 24 bytes)

Examples:
  10 tables: 240 bytes
  100 tables: 2,400 bytes (~2 KB)
  1000 tables: 24,000 bytes (~23 KB)
  4096 tables: 98,304 bytes (~96 KB)
```

#### Table Storage Overhead
```
Per row overhead: ~200-250 bytes (system columns)

Examples:
  1 million rows: ~200-250 MB overhead
  10 million rows: ~2-2.5 GB overhead
  100 million rows: ~20-25 GB overhead
```

---

## Performance Tuning

### Insert Performance Optimization

#### Batch Inserts
```sql
-- Good: Batch INSERT (single transaction)
BEGIN;
INSERT INTO audit_log (user_id, action) VALUES
  (1001, 'login'),
  (1002, 'logout'),
  (1003, 'login'),
  ...
  (1100, 'logout');
COMMIT;

-- Better: Use COPY for bulk loading
COPY audit_log (user_id, action) FROM '/tmp/data.csv' CSV;

-- Best: Multi-row INSERT with batching
INSERT INTO audit_log (user_id, action)
SELECT user_id, action FROM staging_table;
```

#### INSERT Performance Settings
```ini
# postgresql.conf

# Increase WAL buffers for high INSERT throughput
wal_buffers = 32MB

# Asynchronous commit for non-critical data (risks losing last few transactions on crash)
synchronous_commit = off  # Use with caution!

# Group commits
commit_delay = 10  # microseconds
commit_siblings = 5
```

### Hash Computation Optimization

#### Hash Cache Tuning
```c
// Increase hash cache for high concurrency
// File: blockchain_counter.h
#define MAX_CACHED_HASHES 50000  // Default: 10000

// This reduces disk I/O for retrieving previous hashes
// Trade-off: More shared memory usage
```

#### Hash Algorithm Selection (Future)
```sql
-- Future: Choose hash algorithm based on requirements
-- SHA-256: Best balance of security and performance (default)
-- SHA-224: Slightly faster, smaller output
-- SHA-512: More secure, slower
```

### Query Performance Optimization

#### Indexing Strategy
```sql
-- Index on counter for range queries
CREATE INDEX idx_audit_log_lsn ON audit_log (__tx_lsn);

-- BRIN index on timestamp for time-series queries (much smaller)
CREATE INDEX idx_audit_log_ts ON audit_log USING BRIN (__tx_timestamp);

-- Index on row_id for lookups
CREATE INDEX idx_audit_log_rowid ON audit_log (__row_id);

-- Partial index for latest versions only (when versioning implemented)
CREATE INDEX idx_audit_log_latest ON audit_log (user_id)
WHERE __is_latest = true;
```

#### Query Tuning
```sql
-- Good: Use counter for ordering
SELECT * FROM audit_log ORDER BY __tx_lsn DESC LIMIT 100;

-- Bad: Use timestamp for ordering (slower, not guaranteed unique)
SELECT * FROM audit_log ORDER BY __tx_timestamp DESC LIMIT 100;

-- Good: Use counter ranges
SELECT * FROM audit_log WHERE __tx_lsn BETWEEN 1000 AND 2000;

-- Bad: Hash comparisons (slow)
SELECT * FROM audit_log WHERE __curr_hash = '\x1a2b3c...'::bytea;
```

### Counter Performance

#### Counter Persistence Tuning
```c
// File: blockchain_counter.c
// Adjust persistence threshold

// Default: Persist every 100 operations
#define COUNTER_PERSISTENCE_THRESHOLD 100

// High-throughput: Increase to reduce I/O
#define COUNTER_PERSISTENCE_THRESHOLD 1000

// Low-throughput, high-safety: Decrease
#define COUNTER_PERSISTENCE_THRESHOLD 10
```

#### Counter Lock Contention
```
The extension uses per-table counter locks to minimize contention.

Lock hierarchy:
  1. Control lock (brief, for hash table access)
  2. Per-table counter lock (brief, for increment)

Contention scenarios:
  - High: Many concurrent INSERTs to same table
  - Low: INSERTs distributed across multiple tables

Mitigation:
  - Partition data across multiple blockchain tables
  - Use batch INSERTs to reduce lock acquisition frequency
```

---

## Production Recommendations

### Recommended Configuration (Production)

```ini
# postgresql.conf - Production Settings

#------------------------------------------------------------------------------
# MEMORY SETTINGS
#------------------------------------------------------------------------------

# Shared memory
shared_buffers = 2GB  # 25% of system RAM (for 8GB system)
work_mem = 16MB
maintenance_work_mem = 256MB

# Effective cache
effective_cache_size = 6GB  # 75% of system RAM

#------------------------------------------------------------------------------
# WAL SETTINGS
#------------------------------------------------------------------------------

wal_level = logical  # For replication support
wal_buffers = 32MB
max_wal_size = 4GB
min_wal_size = 2GB
checkpoint_timeout = 15min

# Synchronous commit
synchronous_commit = on  # CRITICAL: Set to 'on' for production

#------------------------------------------------------------------------------
# CHECKPOINT SETTINGS
#------------------------------------------------------------------------------

checkpoint_completion_target = 0.9
checkpoint_warning = 5min

#------------------------------------------------------------------------------
# LOGGING
#------------------------------------------------------------------------------

# Enable query logging for audit
log_destination = 'csvlog'
logging_collector = on
log_directory = 'pg_log'
log_filename = 'postgresql-%Y-%m-%d_%H%M%S.log'
log_rotation_age = 1d
log_rotation_size = 100MB

# Log important events
log_min_duration_statement = 1000  # Log queries > 1 second
log_connections = on
log_disconnections = on
log_duration = off
log_statement = 'ddl'  # Log all DDL statements

#------------------------------------------------------------------------------
# AUTOVACUUM
#------------------------------------------------------------------------------

autovacuum = on
autovacuum_max_workers = 4
autovacuum_naptime = 1min
autovacuum_vacuum_scale_factor = 0.05
autovacuum_analyze_scale_factor = 0.02

#------------------------------------------------------------------------------
# CONNECTION SETTINGS
#------------------------------------------------------------------------------

max_connections = 200
```

### Monitoring Configuration

```sql
-- Create monitoring views
CREATE VIEW blockchain_table_stats AS
SELECT
  schemaname,
  tablename,
  pg_size_pretty(pg_total_relation_size(schemaname||'.'||tablename)) AS total_size,
  pg_size_pretty(pg_relation_size(schemaname||'.'||tablename)) AS table_size,
  pg_size_pretty(pg_total_relation_size(schemaname||'.'||tablename) -
                 pg_relation_size(schemaname||'.'||tablename)) AS indexes_size
FROM pg_tables
WHERE tablename IN (
  SELECT tablename FROM pg_tables t
  JOIN pg_class c ON t.tablename = c.relname
  WHERE c.relam = (SELECT oid FROM pg_am WHERE amname = 'blockchain')
);

-- Monitor counter persistence
CREATE VIEW blockchain_counter_status AS
SELECT
  c.oid AS table_oid,
  n.nspname AS schema_name,
  c.relname AS table_name,
  pg_stat_get_live_tuples(c.oid) AS live_tuples
FROM pg_class c
JOIN pg_namespace n ON c.relnamespace = n.oid
WHERE c.relam = (SELECT oid FROM pg_am WHERE amname = 'blockchain');
```

### Backup Configuration

```bash
# Backup script for blockchain tables
#!/bin/bash

# Full database backup
pg_dump -Fc -f /backup/blockchain_db_$(date +%Y%m%d).dump blockchain_db

# Backup counter file
cp $PGDATA/global/blockchain_counters /backup/blockchain_counters_$(date +%Y%m%d)

# Export hash chain for verification
psql -d blockchain_db -c "COPY (
  SELECT __tx_lsn, encode(__curr_hash, 'hex'), encode(__prev_hash, 'hex')
  FROM audit_log ORDER BY __tx_lsn
) TO '/backup/hash_chain_$(date +%Y%m%d).csv' CSV HEADER"
```

### High Availability Configuration

```ini
# postgresql.conf - Primary Server

# Replication
wal_level = logical
max_wal_senders = 10
max_replication_slots = 10
hot_standby = on

# Archive mode
archive_mode = on
archive_command = 'cp %p /archive/%f'

# Replication timeout
wal_sender_timeout = 60s
```

```ini
# postgresql.conf - Replica Server

# Standby mode
hot_standby = on
hot_standby_feedback = on

# Replica settings
max_standby_streaming_delay = 30s
wal_receiver_status_interval = 10s
```

### Security Configuration

```ini
# postgresql.conf

# SSL/TLS
ssl = on
ssl_cert_file = '/path/to/server.crt'
ssl_key_file = '/path/to/server.key'

# Authentication
password_encryption = scram-sha-256
```

```sql
-- pg_hba.conf recommendations
-- Allow only specific users to modify blockchain tables

-- Local connections
local   blockchain_db   blockchain_admin   scram-sha-256

-- Remote connections (SSL required)
hostssl blockchain_db   blockchain_app     10.0.0.0/8   scram-sha-256

-- Read-only replica
hostssl replication     replicator         10.0.0.0/8   scram-sha-256
```

---

## Troubleshooting Configuration Issues

### High Memory Usage

**Symptom**: PostgreSQL using excessive memory

**Diagnosis**:
```sql
SELECT name, setting, unit, context FROM pg_settings
WHERE name IN ('shared_buffers', 'work_mem', 'maintenance_work_mem');
```

**Solution**:
```ini
# Reduce if necessary
shared_buffers = 1GB  # Instead of 2GB
work_mem = 8MB        # Instead of 16MB
```

### Slow INSERT Performance

**Symptom**: INSERTs taking longer than expected

**Diagnosis**:
```sql
-- Check hash cache utilization
-- (Requires custom monitoring function - future feature)

-- Check for lock contention
SELECT * FROM pg_stat_activity
WHERE wait_event_type = 'Lock'
  AND query LIKE '%INSERT INTO%blockchain%';
```

**Solution**:
```c
// Increase hash cache size
#define MAX_CACHED_HASHES 50000  // From 10000

// Recompile and restart
```

### Counter File Corruption

**Symptom**: Counter values reset or inconsistent after restart

**Diagnosis**:
```bash
# Check counter file
ls -lh $PGDATA/global/blockchain_counters

# Check PostgreSQL logs
grep "blockchain" $PGDATA/log/postgresql-*.log
```

**Solution**:
```bash
# Stop PostgreSQL
pg_ctl stop -D $PGDATA

# Remove corrupted counter file (will auto-recover)
rm $PGDATA/global/blockchain_counters

# Restart PostgreSQL (automatic recovery)
pg_ctl start -D $PGDATA
```

### Disk Space Issues

**Symptom**: Disk space filling up rapidly

**Diagnosis**:
```sql
SELECT
  schemaname,
  tablename,
  pg_size_pretty(pg_total_relation_size(schemaname||'.'||tablename)) AS size
FROM pg_tables
ORDER BY pg_total_relation_size(schemaname||'.'||tablename) DESC
LIMIT 20;
```

**Solution**:
```sql
-- Archive old data (manual export, cannot DELETE)
COPY (SELECT * FROM audit_log WHERE __tx_timestamp < NOW() - INTERVAL '1 year')
TO '/archive/audit_log_old.csv' CSV HEADER;

-- Consider partitioning (future feature)
-- Cannot TRUNCATE blockchain tables - need to export and recreate
```

---

## Configuration Checklist

### Pre-Deployment Checklist

- [ ] Compile-time constants reviewed and adjusted
- [ ] Shared memory sized appropriately
- [ ] WAL settings configured for INSERT workload
- [ ] Backup and recovery procedures tested
- [ ] Monitoring and alerting configured
- [ ] Security settings reviewed (SSL, authentication)
- [ ] High availability setup if required
- [ ] Performance baseline established
- [ ] Counter persistence file location verified
- [ ] Disk space monitoring configured

### Post-Deployment Checklist

- [ ] Monitor shared memory usage
- [ ] Monitor INSERT performance
- [ ] Verify counter persistence
- [ ] Test backup and restore procedures
- [ ] Monitor disk space growth
- [ ] Review query performance
- [ ] Check for lock contention
- [ ] Verify hash chain integrity
- [ ] Test high availability failover
- [ ] Review PostgreSQL logs regularly

---

## Summary

### Key Configuration Points

1. **Shared Memory**: Allocate 1-4 MB for blockchain extension
2. **WAL Settings**: Optimize for INSERT-only workload
3. **Autovacuum**: Required for catalogs, not blockchain tables
4. **Indexing**: Create selectively on system columns
5. **Monitoring**: Track table sizes, counter status, performance
6. **Backups**: Include counter file and hash chain exports
7. **Security**: Restrict modification access, use SSL
8. **Performance**: Tune based on workload characteristics

### Default Configuration Summary

```
Compile-Time Defaults:
  MAX_BLOCKCHAIN_TABLES = 1024
  MAX_CACHED_HASHES = 10000
  NUM_BLOCKCHAIN_COLUMNS = 9

Memory Requirements:
  Shared memory: ~753 KB minimum (2 MB recommended)
  Per-row overhead: ~200-250 bytes

Performance Defaults:
  Counter persistence: Every 100 operations
  Hash algorithm: SHA-256
  Synchronous commit: ON (for safety)
```

---

**Related Documentation**:
- [System Columns Reference](system-columns-reference.md) - Storage overhead details
- [SQL Functions Reference](sql-functions-reference.md) - Runtime function usage
- [C API Reference](c-api-reference.md) - Internal implementation details
