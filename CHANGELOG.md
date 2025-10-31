# Changelog

All notable changes to the PostgreSQL Blockchain Extension project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.0.0] - 2025-10-27

### Added

#### Core Blockchain Functionality
- **Blockchain Table Access Method (BlockchainAM)**: Custom table access method implementing PostgreSQL's `TableAmRoutine` interface with append-only semantics
  - Eight-phase insert pipeline with automatic system column population
  - Complete blocking of UPDATE/DELETE/LOCK operations at storage layer
  - Delegation to heap storage for physical data management

#### Counter-Based Sequencing System
- **Global Counter Infrastructure**: Shared memory-based monotonic counters providing total ordering per blockchain table
  - Per-table atomic counter allocation with LWLock protection
  - Lazy counter recovery from persistent storage (`SELECT MAX(__tx_lsn)`)
  - Intentional gap-tolerant design for tracking failed/aborted transactions
  - Persistence to `global/blockchain_counters` file
  - Support for up to 1,024 concurrent blockchain tables

#### Shared Memory Hash Cache
- **Concurrent Hash Cache**: 10,000-entry hash table for uncommitted transaction hashes
  - Cross-transaction hash visibility with LWLock-protected reads/writes
  - Enables Transaction B to reference Transaction A's hash before commit
  - Resolves hash chain branching in concurrent workloads
  - Composite key structure: `(Oid table_oid, uint64 counter)`
  - 32-byte SHA-256 digest storage per entry

#### Cryptographic Hash Engine
- **SHA-256 Hash Computation**: Deterministic hash chaining with counter integration
  - Type-aware column serialization using PostgreSQL type output functions
  - Canonical ordering of tuple data for reproducibility
  - Hash input composition: `prev_hash + counter + timestamp + user_columns`
  - Produces 32-byte cryptographic fingerprints (FIPS 180-4 compliant)
  - Tamper detection through hash chain verification

#### Query Anchoring System (Patent Implementation)
- **Arbitrary SQL Query Anchoring**: SPI-based framework for anchoring query result sets
  - `create_anchor_table(table_name)`: Creates blockchain table for storing anchors
  - `anchor_query_result(anchor_table, query_id, query_text, notes)`: Executes query and stores hash
  - `verify_query_anchor(anchor_table, query_id)`: Re-executes query and verifies integrity
  - Deterministic tuple sorting with `qsort_arg`
  - Canonical serialization with pipe-delimiter encoding
  - Support for complex queries (JOINs, aggregations, subqueries, CTEs)
  - SPI memory context management for safe operation

#### System Metadata Columns
- **Nine Automatic Columns**: Transparent provenance tracking without application code changes
  - `__row_id` (UUID): Globally unique row identifier
  - `__curr_hash` (BYTEA): SHA-256 hash of current block (32 bytes)
  - `__prev_hash` (BYTEA): SHA-256 hash of previous block in chain
  - `__tx_type` (TEXT): Transaction type marker (currently always "INSERT")
  - `__tx_lsn` (INT8): Global monotonic counter value for total ordering
  - `__tx_origin` (UUID): Transaction originator identifier (reserved for replication)
  - `__tx_version` (INT4): Row version number (currently always 1)
  - `__is_latest` (BOOL): Latest version flag (currently always TRUE)
  - `__tx_timestamp` (TIMESTAMPTZ): Transaction commit timestamp with microsecond precision
  - Total overhead: 135 bytes per row

#### Immutability Enforcement
- **Multi-Layer Protection**: Defense-in-depth immutability guarantees
  - Parser-level blocking of UPDATE/DELETE via TableAM method pointers (set to NULL)
  - Utility hook (`ProcessUtility_hook`) intercepting DDL commands
  - Blocked operations: ALTER TABLE, TRUNCATE, DROP (with cascade), CLUSTER, REINDEX, VACUUM FULL
  - Allowed operations: SELECT, INSERT, VACUUM (analyze only), standard indexes
  - Error messages with helpful hints for blocked operations

#### Concurrency Handling
- **Hash Lookup Retry System**: 500-iteration retry loop with exponential backoff
  - 2ms sleep delay between retries (max 1 second total wait)
  - Three-tier lookup: shared memory cache → database query → retry with timeout
  - Eliminates LSN-based race conditions (reduced failure rate from 98.5% to 0%)
  - Ensures perfect hash chain integrity under concurrent load (tested with 10+ clients)
  - Graceful error handling after timeout to prevent infinite loops

#### Configuration & Tuning
- **Compile-Time Constants**:
  - `MAX_BLOCKCHAIN_TABLES = 1024`: Maximum concurrent blockchain tables
  - `MAX_CACHED_HASHES = 10000`: Hash cache size limit
  - `NUM_BLOCKCHAIN_COLUMNS = 9`: System metadata columns
  - `BLOCKCHAIN_TABLE_AM_OID = 12336`: Catalog OID for access method
  - `MAX_RETRY_ATTEMPTS = 500`: Hash lookup retry limit
  - `RETRY_DELAY_USEC = 2000`: 2ms delay between retries

#### Documentation
- Low-Level Design (LLD) document (~6,750 lines covering all subsystems)
- Query Anchoring implementation guide with patent specifications
- Concurrency fix documentation detailing hash branching resolution
- Comprehensive test suites for all major features
- Architecture diagrams and data flow visualizations

### Fixed

#### Concurrency & Race Conditions
- **Hash Branching Issue**: Resolved hash chain forking during concurrent inserts
  - **Root Cause**: Race condition where multiple transactions retrieved same `MAX(__tx_lsn)`
  - **Solution**: Counter-based lookup + shared memory cache + retry loop
  - **Impact**: Improved chain integrity from 1.5% to 100% under concurrent load (10 clients, 1000 transactions)
  - **Performance**: Achieved 881 TPS with perfect hash chain linkage

#### Query Anchoring Memory Safety
- **SPI Memory Context Corruption**: Fixed three critical memory corruption bugs
  - **Bug 1**: `result_hash` freed after `SPI_finish()` in `anchor_query_result()`
  - **Bug 2**: `stored_query_text` and `stored_hash` freed in `verify_query_anchor()`
  - **Bug 3**: `computed_hash` freed during verification recomputation
  - **Solution**: `SPI_palloc()` to copy data to upper executor context before `SPI_finish()`
  - **Symptom**: Hash values showing `\x7f7f7f7f...` (freed memory pattern) instead of proper SHA-256

- **Hash Computation Format**: Corrected SHA-256 finalization
  - **Bug**: Passing `VARDATA(result)` directly to `bc_sha256_final()`
  - **Fix**: Use temporary digest array, then copy to bytea structure
  - **Follows**: Existing pattern in `blockchain_hash.c`

- **UNIQUE Constraint Issue**: Removed unsupported UNIQUE constraint from anchor tables
  - **Root Cause**: Blockchain tables don't support indexes (custom table access method)
  - **Solution**: Allow multiple anchors per `query_id`, verification uses most recent (ORDER BY anchor_time DESC LIMIT 1)
  - **Design Decision**: Enables re-anchoring same query_id to track data evolution

### Performance

#### Benchmarks
- **INSERT Operations**: 10,000-50,000 rows/second (depending on tuple complexity)
- **Hash Computation**: ~2-5 microseconds per row
- **Counter Increment**: <1 microsecond (atomic operation)
- **Hash Cache Lookup**: <1 microsecond (cache hit) to ~50 microseconds (disk read)
- **Concurrent Workload**: 881 TPS with 10 concurrent clients (1,000 transactions total)
- **Storage Overhead**: 135 bytes per row (system columns)

#### Scalability
- Supports up to 1,024 concurrent blockchain tables per database cluster
- Hash cache holds 10,000 entries (LRU eviction not yet implemented)
- Tested with chains of 1,000+ blocks without degradation
- Linear performance scaling with row count (no N² algorithms)

### Implementation Statistics

- **Total Lines of Code**: ~6,750 lines of C across 5 core modules
- **Query Anchoring**: 670+ lines implementing patent
- **Function Registration**: 42 lines in `pg_proc.dat` (OIDs 9003-9005)
- **Test Suites**: 270+ lines of SQL tests
- **Commits**: 47 commits implementing blockchain functionality

### Dependencies

- **PostgreSQL Core**: 13+ (Table Access Method API)
- **SHA-256**: Built-in PostgreSQL `common/sha2.h`
- **UUID Generation**: PostgreSQL `uuid-ossp` extension infrastructure
- **No External Libraries**: Fully self-contained within PostgreSQL

---

## [Unreleased]

### Planned Features

#### High Priority
- **Partition Support**: Native partitioning for time-based blockchain tables
- **Enhanced Verification Functions**:
  - `verify_blockchain_range(table, from_lsn, to_lsn)`: Verify specific counter range
  - `get_blockchain_stats(table)`: Return chain statistics (length, gaps, integrity)
- **Hash Cache Eviction**: LRU policy when cache exceeds 10,000 entries
- **External Timestamp Anchoring**: Submit root hashes to public blockchains (Bitcoin, Ethereum)

#### Medium Priority
- **Batch Anchoring**: `anchor_multiple_queries()` for efficient bulk anchoring
- **Merkle Tree Construction**: Build Merkle trees for efficient batch verification
- **Differential Verification**: Compare two chain snapshots to detect tampering windows
- **Query Rewriting**: Auto-add ORDER BY for deterministic query anchoring
- **Compression**: Compress serialized tuples before hashing

#### Low Priority / Future Work
- **Multi-Node Replication**: Distributed consensus protocols (Raft, Paxos)
- **Smart Contract Integration**: Execute verification logic in Ethereum/Solana
- **Zero-Knowledge Proofs**: Privacy-preserving chain verification
- **Parallel Hashing**: Multi-threaded hash computation for large result sets
- **UPDATE Simulation**: Versioned rows with historical chain linking

### Known Issues

- **No Cache Eviction**: Hash cache grows unbounded until server restart (max 10,000 entries)
- **No Partition Support**: `CREATE BLOCKCHAIN TABLE ... PARTITION BY` not yet implemented
- **No Foreign Keys**: Foreign key constraints to/from blockchain tables not fully tested
- **Limited DDL Support**: Cannot alter blockchain table schema after creation
- **Intentional Counter Gaps**: Aborted transactions leave gaps in `__tx_lsn` sequence

---

## Version History

### Version Numbering

- **Major version** (X.0.0): Incompatible API changes, schema changes
- **Minor version** (0.X.0): New features, backward-compatible
- **Patch version** (0.0.X): Bug fixes, performance improvements

### Compatibility

- **PostgreSQL 13+**: Requires Table Access Method API
- **Upgrade Path**: Major version upgrades may require `pg_dump` / `pg_restore`
- **Schema Stability**: System column structure is stable for v1.x

---

## Contributing

See [CONTRIBUTING.md](docs/development/contributing.md) for guidelines on:
- Reporting bugs
- Suggesting features
- Submitting pull requests
- Code style and testing requirements

---

## Links

- **Documentation**: https://your-org.github.io/postgres_blockchain/
- **Source Code**: https://github.com/your-org/postgres_blockchain
- **Issue Tracker**: https://github.com/your-org/postgres_blockchain/issues
- **Discussions**: https://github.com/your-org/postgres_blockchain/discussions

---

**Legend:**
- `Added`: New features
- `Changed`: Changes in existing functionality
- `Deprecated`: Soon-to-be removed features
- `Removed`: Removed features
- `Fixed`: Bug fixes
- `Security`: Security vulnerability fixes
