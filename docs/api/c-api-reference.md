# C API Reference - PostgreSQL Blockchain Extension

## Overview

This document provides comprehensive reference documentation for the C APIs exposed by the PostgreSQL Blockchain Extension. These APIs are primarily intended for internal use within the PostgreSQL system and for extension developers.

## Header Files

### Primary Headers

```c
#include "blockchain/blockchainam.h"         // Table Access Method interface
#include "blockchain/blockchain_hash.h"      // Hash computation functions
#include "blockchain/blockchain_counter.h"   // Counter management system
#include "commands/blockchain_view.h"        // System column management
```

## Table Access Method API

### Core TAM Interface

```c
/*
 * Get the blockchain table access method routine
 * Returns: Pointer to TableAmRoutine structure
 */
const TableAmRoutine *GetBlockchainTableAmRoutine(void);

/*
 * Global table access method structure
 * Contains all required TAM interface functions
 */
extern const TableAmRoutine blockchainam_methods;
```

### Hash Computation Functions

```c
/*
 * Compute hash for a tuple slot without counter
 * Parameters:
 *   rel: Blockchain table relation
 *   slot: Tuple slot containing data to hash
 *   ts: Transaction timestamp
 *   prev_hash: Previous hash in chain
 * Returns: bytea* containing SHA-256 hash (32 bytes)
 */
bytea *compute_curr_hash(Relation rel, TupleTableSlot *slot,
                        TimestampTz ts, bytea *prev_hash);

/*
 * Compute hash for a tuple slot with counter
 * Parameters:
 *   rel: Blockchain table relation  
 *   slot: Tuple slot containing data to hash
 *   ts: Transaction timestamp
 *   prev_hash: Previous hash in chain
 *   counter: Global counter value
 * Returns: bytea* containing SHA-256 hash (32 bytes)
 */
bytea *compute_curr_hash_with_counter(Relation rel, TupleTableSlot *slot,
                                     TimestampTz ts, bytea *prev_hash,
                                     uint64 counter);

/*
 * Compute SHA-256 hash for slot data
 * Parameters:
 *   rel: Blockchain table relation
 *   slot: Tuple slot containing data
 *   ts: Transaction timestamp
 * Returns: bytea* containing hash
 */
bytea *compute_sha256_slot_hash(Relation rel, TupleTableSlot *slot,
                               TimestampTz ts);
```

### System Column Definitions

```c
/*
 * Blockchain system column definition structure
 */
typedef struct BlockchainColumnDef {
    const char *name;        // Column name
    Oid         type_oid;    // PostgreSQL type OID
    int         typmod;      // Type modifier (-1 if not applicable)
    int         attno;       // Attribute number (-1 if not applicable)
} BlockchainColumnDef;

/*
 * Array of system column definitions
 * Contains all 9 blockchain system columns
 */
extern BlockchainColumnDef blockchain_system_columns[];

/*
 * Number of blockchain system columns
 */
#define NUM_BLOCKCHAIN_COLUMNS 9
```

## Hash Engine API

### SHA-256 Context and Functions

```c
/*
 * SHA-256 context structure
 */
typedef struct bc_sha256_ctx {
    uint32  state[8];                         // Hash state
    uint64  bitcount;                        // Bit counter
    uint8   buffer[BC_SHA256_BLOCK_LENGTH];  // Input buffer
} bc_sha256_ctx;

/*
 * Initialize SHA-256 context
 * Parameters:
 *   ctx: Context to initialize
 */
void bc_sha256_init(bc_sha256_ctx *ctx);

/*
 * Update SHA-256 hash with new data
 * Parameters:
 *   ctx: SHA-256 context
 *   input0: Input data buffer
 *   len: Length of input data
 */
void bc_sha256_update(bc_sha256_ctx *ctx, const uint8 *input0, size_t len);

/*
 * Finalize SHA-256 hash computation
 * Parameters:
 *   ctx: SHA-256 context
 *   dest: Output buffer (must be 32 bytes)
 */
void bc_sha256_final(bc_sha256_ctx *ctx, uint8 *dest);
```

### Hash Constants

```c
/* SHA-256 algorithm constants */
#define BC_SHA224_BLOCK_LENGTH      64
#define BC_SHA224_DIGEST_LENGTH     28
#define BC_SHA256_BLOCK_LENGTH      64
#define BC_SHA256_DIGEST_LENGTH     32
#define BC_SHA384_BLOCK_LENGTH      128
#define BC_SHA384_DIGEST_LENGTH     48
#define BC_SHA512_BLOCK_LENGTH      128
#define BC_SHA512_DIGEST_LENGTH     64

/* Hash storage size including PostgreSQL bytea header */
#define BLOCKCHAIN_HASH_STORAGE_SIZE (VARHDRSZ + BC_SHA256_DIGEST_LENGTH)
```

## Counter Management API

### Core Counter Functions

```c
/*
 * Initialize blockchain counter shared memory
 * Called during server startup
 */
void BlockchainCounterShmemInit(void);

/*
 * Calculate required shared memory size for counters
 * Returns: Size in bytes needed for counter system
 */
Size BlockchainCounterShmemSize(void);

/*
 * Get next counter value for a blockchain table
 * Parameters:
 *   table_oid: OID of the blockchain table
 * Returns: Next sequential counter value
 * Note: This function is thread-safe and atomic
 */
uint64 BlockchainGetNextCounter(Oid table_oid);

/*
 * Persist counter values to disk
 * Called periodically and during shutdown
 */
void BlockchainPersistCounters(void);

/*
 * Restore counter values from disk
 * Called during server startup (optional)
 */
void BlockchainRestoreCounters(void);

/*
 * Remove counter entry for a dropped table
 * Parameters:
 *   table_oid: OID of the dropped table
 */
void BlockchainDropTableCounter(Oid table_oid);
```

### Counter Lifecycle Functions

```c
/*
 * Initialize counter system during startup
 * Sets up shared memory and recovery mechanisms
 */
void BlockchainCounterStartup(void);

/*
 * Shutdown counter system
 * Persists final counter state
 */
void BlockchainCounterShutdown(void);
```

### Hash Cache Functions (Concurrency Support)

```c
/*
 * Store a hash in shared memory for an uncommitted block
 * This allows the next transaction to read it before the block is committed
 *
 * Parameters:
 *   table_oid: OID of the blockchain table
 *   counter: Counter value for this hash
 *   hash: Pointer to 32-byte SHA256 hash
 */
void BlockchainStoreHash(Oid table_oid, uint64 counter, const unsigned char *hash);

/*
 * Retrieve a hash from shared memory cache
 *
 * Parameters:
 *   table_oid: OID of the blockchain table
 *   counter: Counter value to look up
 *   hash_out: Output buffer (must be 32 bytes)
 *
 * Returns: true if hash was found in cache, false otherwise
 */
bool BlockchainGetCachedHash(Oid table_oid, uint64 counter, unsigned char *hash_out);
```

### Counter Data Structures

```c
/*
 * Per-table counter entry in shared memory
 */
typedef struct BlockchainCounterEntry {
    Oid      table_oid;         // Table identifier
    uint64   counter_value;     // Current counter value
    uint64   last_persisted;    // Last persisted value
    LWLock   lock;             // Per-table lock
} BlockchainCounterEntry;

/*
 * Shared memory control structure
 */
typedef struct BlockchainCounterData {
    LWLock   ctl_lock;         // Control lock for hash table
    HTAB    *counter_hash;     // Hash table of counter entries
} BlockchainCounterData;

/*
 * Global shared memory pointer
 * Available after BlockchainCounterShmemInit() is called
 */
extern BlockchainCounterData *BlockchainCounterShmem;
```

### Configuration Constants

```c
/* Maximum number of blockchain tables supported */
#define MAX_BLOCKCHAIN_TABLES 1024

/* Persistence occurs every N counter operations */
#define COUNTER_PERSISTENCE_THRESHOLD 100

/* Counter file format version */
#define COUNTER_FILE_VERSION 1

/* Counter file path within PostgreSQL data directory */
#define COUNTER_FILE_PATH "pg_blockchain_counters"
```

## System Column Management API

### Column Visibility Functions

```c
/*
 * Check if a column name is a blockchain system column
 * Parameters:
 *   colname: Column name to check
 * Returns: true if column is a system column
 */
bool is_blockchain_system_column(const char *colname);

/*
 * Filter system columns from a tuple slot
 * Parameters:
 *   slot: Original tuple slot
 *   rel: Blockchain table relation
 * Returns: New tuple slot with only user columns
 */
TupleTableSlot *filter_blockchain_system_columns(TupleTableSlot *slot,
                                                Relation rel);

/*
 * Check if query explicitly accesses system columns
 * Parameters:
 *   query: Query tree to analyze
 *   rel: Blockchain table relation
 * Returns: true if system columns are explicitly referenced
 */
bool is_explicit_system_column_access(Query *query, Relation rel);
```

## Utility and Hook Functions

### Protection Hook Management

```c
/*
 * Initialize blockchain utility hooks
 * Installs ProcessUtility hook for DDL protection
 */
void blockchain_utility_hook_init(void);

/*
 * Cleanup blockchain utility hooks
 * Removes installed hooks
 */
void blockchain_utility_hook_fini(void);
```

## Error Codes and Messages

### Blockchain-Specific Error Codes

```c
/* Feature not supported errors */
#define ERRCODE_BLOCKCHAIN_UPDATE_NOT_SUPPORTED    ERRCODE_FEATURE_NOT_SUPPORTED
#define ERRCODE_BLOCKCHAIN_DELETE_NOT_SUPPORTED    ERRCODE_FEATURE_NOT_SUPPORTED
#define ERRCODE_BLOCKCHAIN_ALTER_NOT_SUPPORTED     ERRCODE_FEATURE_NOT_SUPPORTED
#define ERRCODE_BLOCKCHAIN_INDEX_NOT_SUPPORTED     ERRCODE_FEATURE_NOT_SUPPORTED

/* Data integrity errors */
#define ERRCODE_BLOCKCHAIN_HASH_MISMATCH          ERRCODE_DATA_CORRUPTED
#define ERRCODE_BLOCKCHAIN_COUNTER_CORRUPTION     ERRCODE_DATA_CORRUPTED
#define ERRCODE_BLOCKCHAIN_CHAIN_BROKEN           ERRCODE_DATA_CORRUPTED

/* System errors */
#define ERRCODE_BLOCKCHAIN_SHMEM_NOT_INITIALIZED  ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE
#define ERRCODE_BLOCKCHAIN_COUNTER_OVERFLOW       ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE
```

### Common Error Messages

```c
/* Standard error messages used throughout the system */
#define BLOCKCHAIN_UPDATE_ERROR_MSG \
    "UPDATE is not supported on blockchain tables"
    
#define BLOCKCHAIN_DELETE_ERROR_MSG \
    "DELETE is not supported on blockchain tables"
    
#define BLOCKCHAIN_ALTER_ERROR_MSG \
    "ALTER TABLE operation not allowed on blockchain tables"
    
#define BLOCKCHAIN_INDEX_ERROR_MSG \
    "Indexes are not supported on blockchain tables"
    
#define BLOCKCHAIN_IMMUTABLE_DETAIL \
    "Blockchain tables are immutable"
    
#define BLOCKCHAIN_INSERT_HINT \
    "Use INSERT to add new data"
```

## Performance and Debugging Functions

### Performance Monitoring

```c
/*
 * Get blockchain table statistics
 * Parameters:
 *   table_oid: OID of blockchain table
 * Returns: Structure containing performance metrics
 */
typedef struct {
    uint64 total_inserts;
    uint64 hash_computations; 
    uint64 counter_operations;
    double avg_insert_time;
    double avg_hash_time;
} BlockchainTableStats;

BlockchainTableStats *get_blockchain_table_stats(Oid table_oid);

/*
 * Reset performance statistics
 * Parameters:
 *   table_oid: OID of table (InvalidOid for all tables)
 */
void reset_blockchain_table_stats(Oid table_oid);
```

### Debugging Functions

```c
/*
 * Verify blockchain hash chain integrity
 * Parameters:
 *   rel: Blockchain table relation
 *   start_counter: Starting counter value (0 for beginning)
 *   end_counter: Ending counter value (0 for all)
 * Returns: true if chain integrity is valid
 */
bool verify_blockchain_integrity(Relation rel, uint64 start_counter,
                                uint64 end_counter);

/*
 * Get detailed hash chain information
 * Parameters:
 *   rel: Blockchain table relation
 *   counter: Counter value to examine
 * Returns: Structure with hash chain details
 */
typedef struct {
    uint64 counter;
    bytea *curr_hash;
    bytea *prev_hash;
    TimestampTz timestamp;
    bool chain_valid;
} HashChainInfo;

HashChainInfo *get_hash_chain_info(Relation rel, uint64 counter);
```

## Memory Management Guidelines

### Memory Context Usage

```c
/*
 * Recommended memory contexts for different operations
 */

/* Use for hash computations (temporary, cleaned up automatically) */
MemoryContext hash_computation_context;

/* Use for counter operations (shared memory, persistent) */  
MemoryContext counter_context;

/* Use for system column operations (query lifetime) */
MemoryContext system_column_context;
```

### Memory Allocation Best Practices

1. **Hash Operations**: Use temporary memory contexts that are automatically cleaned up
2. **Counter Operations**: Use shared memory or long-lived contexts
3. **System Columns**: Use query-lifetime memory contexts
4. **Tuple Slots**: Always use appropriate tuple table slot types

## Thread Safety and Concurrency

### Lock Usage Guidelines

```c
/*
 * Lock hierarchy for blockchain operations
 * Always acquire locks in this order to avoid deadlocks:
 */

1. BlockchainCounterShmem->ctl_lock      /* Control lock (shared memory) */
2. BlockchainCounterEntry->lock          /* Per-table counter lock */
3. Relation AccessShareLock/ExclusiveLock /* Table-level lock */
4. Buffer locks                          /* Page-level locks */
```

### Atomic Operations

- Counter increments are atomic within per-table locks
- Hash computations are thread-safe (no shared state)
- System column operations require appropriate relation locks

## Integration Guidelines

### Extension Integration

For developers creating extensions that work with blockchain tables:

1. **Check Table Type**: Always verify `relkind == RELKIND_BLOCKCHAIN_TABLE`
2. **Respect Immutability**: Never attempt UPDATE/DELETE operations
3. **Handle System Columns**: Be aware of hidden system columns
4. **Use Proper APIs**: Use provided APIs rather than direct access

### Custom Access Methods

For developers creating custom access methods:

1. **Inherit Protection**: Implement similar immutability controls
2. **Use Counter System**: Integrate with global counter for consistency  
3. **Implement Hash Chain**: Use provided hash functions for integrity
4. **Handle System Columns**: Manage system columns appropriately

## Version Compatibility

### API Stability

- **Stable APIs**: Core TAM interface, hash functions, counter management
- **Evolving APIs**: System column management, utility functions
- **Internal APIs**: May change between versions, use with caution

### Migration Guidelines

When upgrading between versions:

1. Check for API changes in release notes
2. Test extensions with new version
3. Update function signatures if needed
4. Verify system column handling

---

This C API reference provides comprehensive documentation for all public interfaces in the PostgreSQL Blockchain Extension, enabling developers to integrate with and extend the blockchain functionality effectively.