# PostgreSQL Configuration

PostgreSQL server configuration for optimal blockchain extension performance.

For extension-specific settings, see [Configuration Guide](configuration.md).

---

## Production-Ready Configuration

Tested configuration supporting 32+ concurrent clients and 100k+ row transactions:

```ini
# postgresql.conf

# ===== MEMORY SETTINGS =====
shared_buffers = 512MB                    # Increased from default 128MB
effective_cache_size = 1GB                # OS + PostgreSQL cache
work_mem = 4MB                            # Per-operation memory
maintenance_work_mem = 64MB               # VACUUM, CREATE INDEX

# ===== CONNECTION SETTINGS =====
max_connections = 100

# ===== EXTENSION LOADING (REQUIRED) =====
shared_preload_libraries = 'blockchain_anchor'

# ===== WRITE-AHEAD LOG =====
wal_level = replica
synchronous_commit = on                   # CRITICAL: Must be ON
fsync = on                                # CRITICAL: Must be ON

# ===== LOGGING =====
logging_collector = on
log_directory = 'log'
log_filename = 'postgresql-%Y-%m-%d_%H%M%S.log'
log_min_messages = log                    # Captures cleanup events
log_rotation_age = 1d
log_rotation_size = 100MB
```

---

## Critical Settings

### synchronous_commit

```ini
synchronous_commit = on  # MUST BE ON
```

**Critical for blockchain:** This setting MUST be `on` to ensure transaction durability before returning success. Disabling risks data loss and breaks blockchain integrity guarantees.

### fsync

```ini
fsync = on  # MUST BE ON
```

**Critical for safety:** Forces data to disk before acknowledging writes. Never disable for blockchain tables.

### shared_preload_libraries

```ini
shared_preload_libraries = 'blockchain_anchor'  # REQUIRED
```

**Required for extension:** Extension must be preloaded to allocate shared memory. Restart required after changing.

**Verification:**

```bash
# After restart
psql -c "SELECT * FROM pg_extension WHERE extname = 'blockchain_anchor';"
```

---

## Memory Configuration

### shared_buffers

**Purpose:** Main PostgreSQL buffer pool for caching data.

**Production setting:** `512MB`

**Scaling guide:**

| Total RAM | Recommended shared_buffers | Supported Clients |
|-----------|---------------------------|-------------------|
| 2 GB | 256–512 MB | 16 |
| 4 GB | 512 MB–1 GB | 32 |
| 8 GB | 1–2 GB | 64+ |
| 16+ GB | 2–4 GB | 128+ |

**Rule of thumb:** 25% of total RAM

### effective_cache_size

**Purpose:** Query planner hint for available cache memory.

**Formula:** `(Total RAM × 0.5) to (Total RAM × 0.75)`

**Examples:**

- 2 GB RAM: `effective_cache_size = 1GB`
- 4 GB RAM: `effective_cache_size = 2GB`
- 8 GB RAM: `effective_cache_size = 4–6GB`

### work_mem

**Purpose:** Memory for sorting and hash operations per query.

| Workload Type | Recommended work_mem |
|--------------|---------------------|
| OLTP (small queries) | 4MB |
| Mixed | 16–32MB |
| OLAP (large queries) | 64–128MB |
| ETL/Batch | 128–256MB |

**Warning:** Each query operation can use this much memory. Total = work_mem × concurrent operations.

---

## Connection Settings

### max_connections

**Production default:** `100`

**Scaling:**

| Concurrent Clients | max_connections | Memory Overhead |
|-------------------|----------------|-----------------|
| 1–50 | 100 | ~1 GB |
| 50–100 | 150 | ~1.5 GB |
| 100–200 | 200 | ~2 GB |
| 200+ | Use connection pooling (pgBouncer) | |

**Connection pooling example:**

```ini
# pgbouncer.ini
[databases]
mydb = host=localhost port=5432 dbname=mydb

[pgbouncer]
pool_mode = transaction
max_client_conn = 1000          # Application connections
default_pool_size = 100         # PostgreSQL connections
```

---

## Performance Tuning by Workload

### Real-Time Audit Logging

```ini
shared_buffers = 512MB
effective_cache_size = 1GB
work_mem = 4MB
max_connections = 100
synchronous_commit = on
```

### ETL / Bulk Import

```ini
shared_buffers = 1GB
effective_cache_size = 2GB
work_mem = 128MB                     # Higher for large sorts
maintenance_work_mem = 1GB           # For indexing
max_connections = 50                 # Fewer connections
checkpoint_timeout = 15min           # Reduce checkpoint frequency
```

### High-Concurrency Web Application

```ini
shared_buffers = 2GB
effective_cache_size = 4GB
work_mem = 16MB
max_connections = 200
synchronous_commit = on
```

---

## Monitoring

### Check Current Configuration

```sql
-- Show key settings
SHOW shared_buffers;
SHOW max_connections;
SHOW synchronous_commit;  -- Must be 'on'

-- Check shared memory usage
SELECT * FROM pg_shmem_allocations WHERE name LIKE '%blockchain%';

-- Check active connections
SELECT count(*) FROM pg_stat_activity;
```

### Check for OOM Issues

```bash
# Should return nothing after fixes
grep -i "out of.*memory" ~/pgsql/data/log/postgresql-*.log

# Check system memory
free -h

# Check PostgreSQL memory usage
ps aux | grep postgres | awk '{sum+=$6} END {print sum/1024 " MB"}'
```

---

## Troubleshooting

### Issue: Out of Shared Memory

**Error:**

```
ERROR:  out of shared memory
```

**Solutions:**

1. Increase `shared_buffers = 1GB`
2. Increase `MAX_CACHED_HASHES` in code
3. Restart PostgreSQL

### Issue: Extension Not Loaded

**Error:**

```
ERROR:  could not access file "blockchain_anchor": No such file or directory
```

**Solution:**

```ini
# Add to postgresql.conf
shared_preload_libraries = 'blockchain_anchor'
```

Then restart (reload is insufficient):

```bash
pg_ctl restart -D ~/pgsql/data
```

---

## Configuration Checklist

### Initial Setup

- [ ] Set `shared_buffers = 512MB` (or appropriate for RAM)
- [ ] Set `shared_preload_libraries = 'blockchain_anchor'`
- [ ] Verify `synchronous_commit = on`
- [ ] Verify `fsync = on`
- [ ] Enable logging
- [ ] Restart PostgreSQL
- [ ] Verify extension loaded

### Production Deployment

- [ ] Tune shared_buffers (25% of RAM)
- [ ] Set effective_cache_size (50–75% of RAM)
- [ ] Configure max_connections for concurrency
- [ ] Set work_mem based on workload
- [ ] Enable comprehensive logging
- [ ] Set up connection pooling if >100 clients
- [ ] Test with realistic workload

---

## Related Documentation

- [Configuration Guide](configuration.md) - Extension configuration
- [Troubleshooting](troubleshooting.md) - Common issues
- [Performance Benchmarks](../testing/performance.md) - Performance data
- [Shared Memory Architecture](../architecture/shared-memory.md) - Memory details

---

For installation instructions, see [Installation Guide](installation.md).
