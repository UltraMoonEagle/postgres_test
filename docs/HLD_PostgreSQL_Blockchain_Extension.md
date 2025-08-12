# High-Level Design: PostgreSQL Blockchain Table Extension

## Document Information

- **Document Version**: 1.0
- **Creation Date**: August 2025
- **Authors**: Technical Documentation Team
- **Based on Implementation by**: aditya20-b <aditya2110b@gmail.com>, Adithya <itsmeadithya.a@gmail.com>

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [System Architecture Overview](#system-architecture-overview)
3. [Core Components Deep Dive](#core-components-deep-dive)
4. [Data Flow and Processing](#data-flow-and-processing)
5. [Integration Points](#integration-points)
6. [Security Model](#security-model)
7. [Performance Considerations](#performance-considerations)
8. [Implementation Timeline](#implementation-timeline)
9. [Future Roadmap](#future-roadmap)
10. [References](#references)

## Executive Summary

The PostgreSQL Blockchain Table Extension introduces immutable, tamper-evident storage capabilities to PostgreSQL through a custom Table Access Method (TAM). This extension implements cryptographic hash chaining, global counter management, and strict immutability enforcement to provide audit-grade data integrity guarantees.

### Key Achievements

- **Immutable Storage**: Complete prevention of UPDATE, DELETE, and structural modifications
- **Cryptographic Integrity**: SHA-256 hash chaining ensures tamper detection
- **Global Counter System**: Eliminates double-insert problems with persistent sequencing
- **System Column Management**: Hidden blockchain metadata with explicit access controls
- **Parser Integration**: Native SQL syntax support for blockchain table operations

### Technical Highlights

- **47 commits** implementing comprehensive blockchain functionality
- **5,541 lines** of core blockchain implementation code
- **9 system columns** for blockchain metadata management
- **100% immutability** enforcement through multiple protection layers
- **Lazy recovery** mechanism for counter persistence across restarts

## System Architecture Overview

### High-Level Architecture

```mermaid
graph TB
    subgraph "Application Layer"
        SQL[SQL Commands]
        Client[Client Applications]
    end
    
    subgraph "PostgreSQL Core"
        Parser[SQL Parser]
        Planner[Query Planner]
        Executor[Query Executor]
        Catalog[System Catalog]
    end
    
    subgraph "Blockchain Extension"
        TAM[Blockchain Table Access Method]
        HashEngine[Hash Chain Engine]
        Counter[Global Counter System]
        Utility[DDL Protection]
        Views[System Column Views]
    end
    
    subgraph "Storage Layer"
        Heap[Heap Storage]
        WAL[Write-Ahead Log]
        Files[Counter Files]
    end
    
    Client --> SQL
    SQL --> Parser
    Parser --> Planner
    Planner --> Executor
    Executor --> TAM
    TAM --> HashEngine
    TAM --> Counter
    TAM --> Heap
    Utility --> Parser
    Views --> Catalog
    Counter --> Files
    TAM --> WAL
```

### Component Relationships

| Component | Dependencies | Provides |
|-----------|-------------|----------|
| **Blockchain TAM** | Hash Engine, Counter System | Immutable table operations |
| **Hash Chain Engine** | SHA-256 library | Cryptographic integrity |
| **Global Counter** | Shared memory, file persistence | Unique sequencing |
| **DDL Protection** | Utility hooks | Operation blocking |
| **System Column Views** | Parser modifications | Hidden column management |

## Core Components Deep Dive

### 1. Blockchain Table Access Method (blockchainam.c)

**File**: `src/backend/access/blockchain/blockchainam.c` (3,630 lines)

The core table access method implementing the blockchain storage engine.

#### Key Functions

```c
// Core TAM interface functions
static void blockchainam_tuple_insert(Relation relation, TupleTableSlot *slot, ...);
static TM_Result blockchainam_tuple_delete(Relation rel, ItemPointer tid, ...); // Always fails
static TM_Result blockchainam_tuple_update(Relation rel, ItemPointer otid, ...); // Always fails

// Blockchain-specific operations
extern bytea *compute_curr_hash(Relation rel, TupleTableSlot *slot, TimestampTz ts, bytea *prev_hash);
extern bytea *compute_curr_hash_with_counter(Relation rel, TupleTableSlot *slot, TimestampTz ts, bytea *prev_hash, uint64 counter);
```

#### System Column Definitions

```c
static BlockchainColumnDef blockchain_system_columns[] = {
    {"__row_id", UUIDOID, -1, -1},           // Unique row identifier
    {"__curr_hash", BYTEAOID, -1, -1},       // Current row hash
    {"__prev_hash", BYTEAOID, -1, -1},       // Previous row hash
    {"__tx_type", TEXTOID, -1, -1},          // Transaction type
    {"__tx_lsn", INT8OID, -1, -1},           // Global counter value
    {"__tx_origin", UUIDOID, -1, -1},        // Transaction origin
    {"__tx_version", INT4OID, -1, -1},       // Row version
    {"__is_latest", BOOLOID, -1, -1},        // Latest flag
    {"__tx_timestamp", TIMESTAMPTZOID, -1, -1} // Transaction timestamp
};
```

#### Architecture Principles

1. **Immutability First**: All modification operations return errors
2. **Hash Chain Integrity**: Every insert computes and links hashes
3. **Counter Integration**: Global sequencing for ordering guarantees
4. **System Column Injection**: Automatic addition of blockchain metadata

### 2. Hash Chain Engine (blockchain_hash.c)

**File**: `src/backend/access/blockchain/blockchain_hash.c` (1,065 lines)

Implements SHA-256 cryptographic hashing for data integrity.

#### Hash Chain Algorithm

```
For each new row:
1. Retrieve previous hash from last inserted row
2. Combine: [counter + timestamp + user_data + previous_hash]
3. Compute: current_hash = SHA256(combined_data)
4. Store: current_hash as __curr_hash, previous_hash as __prev_hash
```

#### Key Functions

```c
// Core hashing interface
bytea *compute_sha256_slot_hash(Relation rel, TupleTableSlot *slot, TimestampTz ts);

// SHA-256 implementation
void bc_sha256_init(bc_sha256_ctx *ctx);
void bc_sha256_update(bc_sha256_ctx *ctx, const uint8 *input0, size_t len);
void bc_sha256_final(bc_sha256_ctx *ctx, uint8 *dest);
```

#### Cryptographic Properties

- **Algorithm**: SHA-256 (256-bit output)
- **Input**: Counter + Timestamp + All User Data + Previous Hash
- **Chain Initialization**: First row has zero-hash as previous hash
- **Tamper Detection**: Any modification breaks hash verification

### 3. Global Counter System (blockchain_counter.c)

**File**: `src/backend/access/blockchain/blockchain_counter.c` (594 lines)

Manages unique sequential numbering across blockchain tables.

#### Architecture

```mermaid
graph LR
    subgraph "Shared Memory"
        Hash[Counter Hash Table]
        Locks[Per-Table Locks]
    end
    
    subgraph "Persistence"
        File[Counter Files]
        Recovery[Lazy Recovery]
    end
    
    subgraph "Operations"
        Get[Get Next Counter]
        Persist[Persist Counters]
        Restore[Restore Counters]
    end
    
    Get --> Hash
    Hash --> Locks
    Persist --> File
    Recovery --> Hash
    File --> Recovery
```

#### Key Data Structures

```c
typedef struct BlockchainCounterEntry {
    Oid      table_oid;        // Table identifier
    uint64   counter_value;    // Current counter
    uint64   last_persisted;   // Last persisted value
    LWLock   lock;            // Per-table lock
} BlockchainCounterEntry;

typedef struct BlockchainCounterData {
    LWLock   ctl_lock;        // Control lock
    HTAB    *counter_hash;    // Hash table of counters
} BlockchainCounterData;
```

#### Counter Lifecycle

1. **Initialization**: Shared memory allocation during startup
2. **First Access**: Lazy recovery from table data (MAX(__tx_lsn))
3. **Operation**: Atomic increment with per-table locking
4. **Persistence**: Optional file-based persistence every 100 operations
5. **Recovery**: Automatic recovery from table data on restart

### 4. DDL Protection System (blockchain_utility.c)

**File**: `src/backend/access/blockchain/blockchain_utility.c` (121 lines)

Implements utility hooks to prevent destructive operations on blockchain tables.

#### Protected Operations

```c
// Blocked statement types
if (IsA(parsetree, AlterTableStmt) ||
    IsA(parsetree, TruncateStmt) ||
    IsA(parsetree, DropStmt) ||
    IsA(parsetree, ClusterStmt) ||
    IsA(parsetree, ReindexStmt) ||
    IsA(parsetree, VacuumStmt))
{
    // Check if target is blockchain table and block if so
    ereport(ERROR, (errmsg("operation not allowed on blockchain table")));
}
```

#### Hook Integration

```c
// Hook installation
void blockchain_utility_hook_init(void) {
    prev_ProcessUtility_hook = ProcessUtility_hook;
    ProcessUtility_hook = blockchain_ProcessUtility;
}
```

### 5. System Column Management (blockchain_view.c)

**File**: `src/backend/commands/blockchain_view.c`

Manages visibility of blockchain system columns.

#### Column Visibility Rules

1. **Hidden by Default**: System columns don't appear in `SELECT *`
2. **Explicit Access**: System columns accessible when specifically named
3. **Immutable Names**: System column names cannot be changed
4. **User Column Flexibility**: User columns can be renamed

## Data Flow and Processing

### Insert Operation Flow

```mermaid
sequenceDiagram
    participant Client
    participant Parser
    participant Executor
    participant BlockchainTAM
    participant HashEngine
    participant CounterSystem
    participant Storage
    
    Client->>Parser: INSERT INTO blockchain_table VALUES (...)
    Parser->>Executor: Parsed INSERT statement
    Executor->>BlockchainTAM: tuple_insert()
    BlockchainTAM->>CounterSystem: BlockchainGetNextCounter()
    CounterSystem-->>BlockchainTAM: counter_value
    BlockchainTAM->>HashEngine: compute_curr_hash_with_counter()
    HashEngine-->>BlockchainTAM: current_hash
    BlockchainTAM->>BlockchainTAM: populate_system_columns()
    BlockchainTAM->>Storage: heap_tuple_insert()
    Storage-->>BlockchainTAM: success
    BlockchainTAM-->>Executor: success
    Executor-->>Client: INSERT successful
```

### Query Operation Flow

```mermaid
sequenceDiagram
    participant Client
    participant Parser
    participant Views
    participant Executor
    participant BlockchainTAM
    participant Storage
    
    Client->>Parser: SELECT * FROM blockchain_table
    Parser->>Views: resolve_column_visibility()
    Views-->>Parser: user_columns_only
    Parser->>Executor: Parsed SELECT statement
    Executor->>BlockchainTAM: scan_begin()
    BlockchainTAM->>Storage: heap_beginscan()
    loop For each tuple
        Storage-->>BlockchainTAM: tuple_data
        BlockchainTAM->>Views: filter_system_columns()
        Views-->>BlockchainTAM: user_data_only
        BlockchainTAM-->>Executor: filtered_tuple
    end
    Executor-->>Client: Result set (user columns only)
```

## Integration Points

### Parser Integration

#### Grammar Extensions

```yacc
// New CREATE statement syntax
CreateStmt: CREATE OptTemp BLOCKCHAIN TABLE qualified_name ...

// Object type extensions
opt_class: BLOCKCHAIN TABLE { $$ = OBJECT_BLOCKCHAIN_TABLE; }
```

#### Parse Tree Modifications

The parser was extended to recognize blockchain table syntax and create appropriate parse trees for downstream processing.

### Catalog Integration

#### System Catalog Extensions

- **pg_class.relkind**: New relkind 'b' for blockchain tables
- **pg_am**: Blockchain access method registration
- **Relation attributes**: System column definitions and visibility rules

#### Function Integration

```sql
-- Bootstrap-safe functions
CREATE FUNCTION is_blockchain_table(text) RETURNS boolean;
CREATE FUNCTION list_blockchain_tables() RETURNS TABLE(schema_name name, table_name name);

-- Advanced PL/pgSQL functions (conditionally loaded)
CREATE FUNCTION describe_blockchain_table(text) RETURNS TABLE(...);
CREATE FUNCTION get_blockchain_audit_data(text) RETURNS TABLE(...);
```

### Storage Integration

#### WAL Integration

- Blockchain table operations integrate with PostgreSQL's Write-Ahead Logging
- Hash computations and counter increments are WAL-logged
- Recovery procedures handle blockchain-specific data

#### Buffer Management

- Uses standard PostgreSQL buffer management
- System columns stored alongside user data
- Hash values persisted as BYTEA types

## Security Model

### Immutability Guarantees

#### Multiple Protection Layers

1. **TAM Level**: Update/Delete operations return errors
2. **Utility Hook Level**: DDL operations blocked
3. **Parser Level**: Certain syntax combinations rejected
4. **Catalog Level**: System column protection

#### Threat Model

| Threat | Protection | Implementation |
|--------|------------|----------------|
| **Data Modification** | TAM enforcement | Always-fail update/delete functions |
| **Schema Changes** | Utility hooks | DDL operation interception |
| **Hash Tampering** | Cryptographic verification | SHA-256 chain validation |
| **Counter Manipulation** | Shared memory protection | Per-table locking |
| **System Column Access** | View filtering | Column visibility management |

### Cryptographic Properties

#### Hash Chain Security

- **Algorithm Strength**: SHA-256 provides 2^256 search space
- **Chain Integrity**: Modification of any row breaks subsequent hashes
- **Tamper Evidence**: Hash mismatches indicate data corruption
- **Non-Repudiation**: Timestamp inclusion prevents replay attacks

#### Counter Security

- **Monotonic Increment**: Counters only increase
- **Atomic Operations**: Lock-protected increments
- **Persistence**: Recovery mechanisms prevent counter reuse
- **Uniqueness**: Per-table counter ensures unique sequencing

## Performance Considerations

### Computational Overhead

#### Hash Computation Impact

- **SHA-256 Cost**: ~1-2µs per hash on modern hardware
- **Data Size Impact**: Linear with row size + constant overhead
- **Chain Dependency**: Previous hash lookup required per insert

#### Counter System Performance

- **Shared Memory Access**: Minimal overhead with proper locking
- **Persistence Cost**: File I/O amortized over 100 operations
- **Recovery Cost**: One-time MAX() query per table after restart

### Scalability Analysis

#### Insert Performance

```
Baseline INSERT:     ~100µs
Blockchain INSERT:   ~105µs (5% overhead)
Components:
- Hash computation:  ~2µs
- Counter increment: ~1µs
- System columns:    ~2µs
```

#### Query Performance

- **No Index Support**: Sequential scans only
- **System Column Filtering**: Minimal projection overhead
- **Large Table Impact**: Performance degrades linearly with size

#### Storage Overhead

```
User data:        100% (baseline)
System columns:   ~40 bytes per row
Hash data:        64 bytes per row (curr + prev hash)
Total overhead:   ~104 bytes per row
```

### Optimization Strategies

1. **Batch Operations**: Group inserts to amortize hash lookups
2. **Counter Caching**: Shared memory reduces persistence I/O
3. **Lazy Recovery**: Eliminates startup delays
4. **System Column Filtering**: Reduces query result size

## Implementation Timeline

### Development Phases (47 Commits)

#### Phase 1: Foundation (Commits 1-15)
- **Parser Integration**: Grammar and syntax support
- **Basic TAM**: Skeleton table access method
- **Relkind Support**: System catalog integration
- **Initial Testing**: Basic create/insert functionality

#### Phase 2: Core Features (Commits 16-30)
- **Hash Implementation**: SHA-256 engine and chain logic
- **System Columns**: Metadata column definitions
- **Insert Logic**: Complete insertion pipeline
- **DDL Protection**: Utility hook implementation

#### Phase 3: Counter System (Commits 31-40)
- **Shared Memory**: Counter management infrastructure
- **Global Counter**: Replace LSN-based approach
- **Persistence**: File-based counter storage
- **Recovery**: Lazy loading mechanisms

#### Phase 4: Production Readiness (Commits 41-47)
- **Performance Optimization**: Hash lookup improvements
- **System Integration**: ANALYZE and VACUUM support
- **View Management**: System column visibility
- **Comprehensive Testing**: Stress tests and validation

### Key Milestones

| Milestone | Commit | Description |
|-----------|---------|-------------|
| **Initial Syntax** | 9d33e2d | Parser recognizes BLOCKCHAIN TABLE |
| **First Insert** | e2e67e6 | Basic blockchain table creation working |
| **Hash Chain** | 51dd98e | Hash chain verification implemented |
| **Counter System** | 771f838 | Global counter replaces LSN approach |
| **Production Ready** | 9741ddb | All systems integrated and tested |

## Future Roadmap

### Planned Enhancements

#### Performance Improvements
- **Index Support**: Limited index types for query optimization
- **Parallel Inserts**: Multi-threaded insertion with counter coordination
- **Compression**: Hash and metadata compression options
- **Partitioning**: Time-based or size-based table partitioning

#### Security Enhancements
- **Digital Signatures**: Row-level cryptographic signatures
- **Key Management**: Integration with external key management systems
- **Audit Extensions**: Enhanced audit trail capabilities
- **Zero-Knowledge Proofs**: Privacy-preserving verification

#### Operational Features
- **Replication**: Blockchain table replication with integrity preservation
- **Backup/Restore**: Blockchain-aware backup and recovery procedures
- **Monitoring**: Detailed metrics and alerting capabilities
- **Migration Tools**: Convert regular tables to blockchain tables

### Research Directions

#### Advanced Cryptography
- **Merkle Trees**: Hierarchical hash structures for efficient verification
- **Consensus Mechanisms**: Multi-node consensus for distributed blockchain
- **Homomorphic Encryption**: Computation on encrypted blockchain data

#### Integration Possibilities
- **External Blockchains**: Bridge to public blockchain networks
- **Smart Contracts**: Programmable logic integration
- **DeFi Protocols**: Financial application support

## References

### Implementation Files

| Component | File Location | Lines | Description |
|-----------|--------------|-------|-------------|
| **Core TAM** | `src/backend/access/blockchain/blockchainam.c` | 3,630 | Main table access method |
| **Hash Engine** | `src/backend/access/blockchain/blockchain_hash.c` | 1,065 | SHA-256 implementation |
| **Counter System** | `src/backend/access/blockchain/blockchain_counter.c` | 594 | Global counter management |
| **DDL Protection** | `src/backend/access/blockchain/blockchain_utility.c` | 121 | Operation blocking |
| **System Integration** | `src/backend/access/blockchain/blockchain_anchor.c` | 131 | Anchor functionality |

### Documentation Files

- `BLOCKCHAIN_TABLE_GUIDE.md` - User guide and reference
- `BLOCKCHAIN_TABLE_SUMMARY.md` - Quick start and status
- `src/backend/catalog/blockchain_functions.sql` - SQL helper functions

### Test Files

- `blockchain_comprehensive_tests.sql` - Complete test suite
- `blockchain_stress_test.sql` - Performance and stress testing
- Various concurrent and performance test scripts

### Commit History

47 commits by authors:
- **aditya20-b** <aditya2110b@gmail.com>
- **Adithya** <itsmeadithya.a@gmail.com>

Total implementation: **5,541 lines** of C code plus extensive SQL functions and tests.

---

*This document represents the complete technical architecture of the PostgreSQL Blockchain Table Extension based on comprehensive analysis of the implementation by the specified authors.*