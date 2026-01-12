# Data Flow Architecture

!!! summary "One-Minute Summary"
    - **INSERT operations** follow an 8-phase pipeline: counter allocation → hash chain linkage → SHA-256 computation → cache storage → system column population → heap insertion
    - **Query anchoring** executes arbitrary SQL, deterministically sorts/serializes results, computes SHA-256, and stores in blockchain table with metadata
    - **Verification flow** re-executes queries and compares hashes to detect tampering, achieving 100% integrity under concurrent load

---

## INSERT Operation Complete Flow

The INSERT operation is the cornerstone of the blockchain table implementation, consisting of eight distinct phases that transform user data into an immutable, cryptographically-linked block.

### Phase-by-Phase INSERT Pipeline

```mermaid
flowchart TD
    A[SQL INSERT Statement] --> B[Phase 1: Initialization]
    B --> C[Phase 2: Counter Allocation]
    C --> D[Phase 3: Tuple Slot Preparation]
    D --> E[Phase 4: Hash Chain Linkage]
    E --> F[Phase 5: System Column Population]
    F --> G[Phase 6: Physical Storage]
    G --> H[Phase 7: Backend Context Update]
    H --> I[Phase 8: Periodic Cleanup]
    I --> J[INSERT Complete]

    style A fill:#e1f5ff
    style E fill:#fff4e1
    style F fill:#e8f5e9
    style J fill:#f3e5f5
```

### Detailed INSERT Flow with Timing

```mermaid
sequenceDiagram
    participant Client
    participant Parser
    participant Executor
    participant BlockchainAM
    participant CounterSystem
    participant HashCache
    participant HeapStorage

    Client->>Parser: INSERT INTO blockchain_table VALUES (...)
    Parser->>Executor: Parse & Plan (add 9 system columns)
    Executor->>BlockchainAM: blockchainam_tuple_insert()

    Note over BlockchainAM: Phase 1: Initialization (~1μs)
    BlockchainAM->>BlockchainAM: Generate UUID (__row_id)
    BlockchainAM->>BlockchainAM: Capture timestamp

    Note over BlockchainAM,CounterSystem: Phase 2: Counter Allocation (~1μs)
    BlockchainAM->>CounterSystem: BlockchainGetNextCounter(table_oid)
    CounterSystem->>CounterSystem: Acquire LWLock
    CounterSystem->>CounterSystem: Atomic increment counter
    CounterSystem->>CounterSystem: Release LWLock
    CounterSystem-->>BlockchainAM: counter = 42

    Note over BlockchainAM,HashCache: Phase 4: Hash Chain Linkage (2ms - 50ms)
    BlockchainAM->>HashCache: BlockchainGetCachedHash(table_oid, counter-1)
    alt Hash found in cache (fast path)
        HashCache-->>BlockchainAM: prev_hash (32 bytes) [<1μs]
    else Hash not in cache (slow path)
        BlockchainAM->>HeapStorage: SPI Query: SELECT __curr_hash WHERE __tx_lsn=41
        HeapStorage-->>BlockchainAM: prev_hash (32 bytes) [2-50ms]
    end

    Note over BlockchainAM: Phase 4b: Compute Current Hash (~2μs)
    BlockchainAM->>BlockchainAM: bc_sha256_init()
    BlockchainAM->>BlockchainAM: bc_sha256_update(prev_hash)
    BlockchainAM->>BlockchainAM: bc_sha256_update(counter)
    BlockchainAM->>BlockchainAM: bc_sha256_update(timestamp)
    BlockchainAM->>BlockchainAM: bc_sha256_update(user_data)
    BlockchainAM->>BlockchainAM: bc_sha256_final() → curr_hash

    Note over BlockchainAM,HashCache: Phase 4c: Cache Storage (<1μs)
    BlockchainAM->>HashCache: BlockchainStoreHash(table_oid, counter, curr_hash)
    HashCache->>HashCache: Store in shared memory
    HashCache-->>BlockchainAM: OK

    Note over BlockchainAM: Phase 5: System Columns (~5μs)
    BlockchainAM->>BlockchainAM: Set __curr_hash = curr_hash
    BlockchainAM->>BlockchainAM: Set __prev_hash = prev_hash
    BlockchainAM->>BlockchainAM: Set __tx_lsn = 42
    BlockchainAM->>BlockchainAM: Set __tx_type = "INSERT"
    BlockchainAM->>BlockchainAM: Set __tx_timestamp = now
    BlockchainAM->>BlockchainAM: Set __tx_version, __is_latest, etc.

    Note over BlockchainAM,HeapStorage: Phase 6: Heap Insert (~100μs)
    BlockchainAM->>HeapStorage: heap_insert(complete_tuple)
    HeapStorage->>HeapStorage: Write to data file
    HeapStorage->>HeapStorage: Write to WAL
    HeapStorage-->>BlockchainAM: ItemPointer (page, offset)

    BlockchainAM-->>Executor: Success
    Executor-->>Client: INSERT 0 1
```

### Performance Characteristics

| Phase | Operation | Average Latency | Bottleneck |
|-------|-----------|-----------------|------------|
| **Phase 1** | Initialization | ~1 μs | UUID generation |
| **Phase 2** | Counter Allocation | ~1 μs | LWLock contention (multi-table parallelizes) |
| **Phase 3** | Slot Preparation | ~5 μs | Memory allocation |
| **Phase 4a** | Previous Hash Retrieval | <1 μs (cache hit)<br>2-50 ms (cache miss) | Database query for committed hash |
| **Phase 4b** | SHA-256 Computation | ~2 μs | CPU-bound cryptographic hash |
| **Phase 4c** | Hash Cache Storage | <1 μs | Shared memory write with LWLock |
| **Phase 5** | System Column Population | ~5 μs | Datum construction |
| **Phase 6** | Heap Storage | ~100 μs | Disk I/O and WAL writes |
| **Phase 7** | Backend Context Update | <1 μs | Memory write |
| **Phase 8** | Periodic Cleanup | 0 μs (every 100th insert: ~10 μs) | Memory context reset |
| **TOTAL** | End-to-End Latency | **106 μs** (cache hit)<br>**2-50 ms** (cache miss) | Hash lookup on cache miss |

!!! success "Benchmark Results"
    - **Concurrent Insert Throughput**: 881 TPS (10 concurrent clients)
    - **Single-Threaded Latency**: 106 μs average
    - **Baseline Heap Latency**: 100 μs (standard PostgreSQL)
    - **Blockchain Overhead**: 6% additional latency
    - **Hash Chain Integrity**: 100% (zero broken links in stress tests)

### Retry Loop Mechanism

The retry loop in Phase 4a handles timing windows where Transaction B needs the hash from Transaction A before A has committed.

```mermaid
flowchart TD
    A[Need prev_hash for counter N-1] --> B{Check Hash Cache}
    B -->|Found| C[Return prev_hash immediately]
    B -->|Not Found| D{Query Database}
    D -->|Found| E[Return prev_hash from DB]
    D -->|Not Found| F{Retry < 500?}
    F -->|Yes| G[Sleep 2ms]
    G --> B
    F -->|No| H[ERROR: Timeout waiting for prev_hash]

    style C fill:#4caf50,color:#fff
    style E fill:#4caf50,color:#fff
    style H fill:#f44336,color:#fff
```

**Retry Parameters:**
- **Max Retries**: 500 iterations
- **Sleep Duration**: 2 milliseconds per retry
- **Total Timeout**: 1 second maximum
- **Success Rate**: 99.9%+ (hash typically found within 1-2 retries)

---

## Query Anchoring Flow

Query anchoring extends blockchain integrity guarantees to arbitrary SQL query results, enabling regulatory compliance and ML dataset provenance.

### Query Anchoring Architecture

```mermaid
flowchart TB
    subgraph UserInput [User Input]
        A[anchor_query_result<br/>anchor_table, query_id,<br/>query_text, notes]
    end

    subgraph Execution [Query Execution]
        B[SPI_connect]
        C[SPI_execute query_text]
        D[Retrieve SPI_tuptable]
        E[Get row count SPI_processed]
    end

    subgraph Hashing [Deterministic Hashing]
        F[Sort tuples lexicographically]
        G[Serialize: col1|col2|col3...]
        H[bc_sha256_init]
        I[bc_sha256_update query_text]
        J[bc_sha256_update tuple_1]
        K[bc_sha256_update tuple_2...N]
        L[bc_sha256_final → result_hash]
    end

    subgraph Storage [Blockchain Storage]
        M[Copy hash to upper memory context]
        N[SPI_finish]
        O[INSERT INTO anchor_table<br/>query_id, query_text,<br/>result_hash, timestamp,<br/>user_id, notes]
        P[Blockchain insert<br/>adds 9 system columns]
    end

    A --> B --> C --> D --> E
    E --> F --> G --> H --> I --> J --> K --> L
    L --> M --> N --> O --> P
    P --> Q[Return result_hash]

    style A fill:#e3f2fd
    style L fill:#fff9c4
    style P fill:#c8e6c9
    style Q fill:#f3e5f5
```

### Deterministic Result Hashing Algorithm

The patent implementation ensures reproducible hashes regardless of query execution order.

```mermaid
graph TD
    subgraph Input ["Input Data"]
        A["Query Result<br>Row 3: B, 300<br>Row 1: A, 100<br>Row 2: C, 200"]
    end

    subgraph Sort ["Deterministic Sorting"]
        B["qsort_arg with<br>compare_tuples"]
        C["Sorted Result<br>Row 1: A, 100<br>Row 2: C, 200<br>Row 3: B, 300"]
    end

    subgraph Serialize ["Serialization"]
        D["Tuple 1: A|100"]
        E["Tuple 2: C|200"]
        F["Tuple 3: B|300"]
    end

    subgraph Hash ["SHA-256 Computation"]
        G["SHA-256 Context"]
        H["Update: query_text"]
        I["Update: A|100"]
        J["Update: C|200"]
        K["Update: B|300"]
        L["Finalize: 32-byte hash"]
    end

    A --> B --> C
    C --> D
    C --> E
    C --> F
    D --> G
    G --> H
    H --> I
    I --> J
    J --> K
    K --> L
    L --> M["0x3abb974851d8201b..."]

    style A fill:#ffebee
    style C fill:#e8f5e9
    style L fill:#fff9c4
    style M fill:#e1bee7
```

**Sorting Rules:**
- **Column-by-column comparison** (left to right)
- **Type-specific comparison** using PostgreSQL type output functions
- **NULL handling**: NULLs sort last (consistent with `ORDER BY ... NULLS LAST`)
- **Lexicographic ordering** for text types
- **Numeric ordering** for integer/decimal types

**Serialization Format:**
```
"col1_value|col2_value|col3_value|...|colN_value"
```

- **Delimiter**: `|` (pipe character)
- **NULL representation**: `<NULL>` string
- **Type conversion**: Via PostgreSQL `OidOutputFunctionCall()`

!!! warning "Query Determinism"
    Queries using non-deterministic functions (`RANDOM()`, `NOW()`, `CURRENT_TIMESTAMP`) will produce different hashes on each execution. Use deterministic queries for anchoring.

---

## Verification Flow

Verification re-executes the anchored query and compares the computed hash with the stored hash to detect data tampering.

### Verification Sequence Diagram

```mermaid
sequenceDiagram
    participant Client
    participant VerifyFunc as verify_query_anchor()
    participant AnchorTable as Anchor Blockchain Table
    participant SPI as PostgreSQL SPI
    participant HashEngine as Hash Engine

    Client->>VerifyFunc: verify_query_anchor('anchors', 'q1')

    Note over VerifyFunc,AnchorTable: Step 1: Retrieve Anchor
    VerifyFunc->>SPI: SPI_connect()
    VerifyFunc->>AnchorTable: SELECT query_text, result_hash WHERE query_id='q1'
    AnchorTable-->>VerifyFunc: stored_query_text, stored_hash
    VerifyFunc->>VerifyFunc: Copy to upper memory context
    VerifyFunc->>SPI: SPI_finish()

    Note over VerifyFunc,SPI: Step 2: Re-execute Query
    VerifyFunc->>SPI: SPI_connect()
    VerifyFunc->>SPI: SPI_execute(stored_query_text, true, 0)
    SPI-->>VerifyFunc: SPI_tuptable, SPI_processed

    Note over VerifyFunc,HashEngine: Step 3: Recompute Hash
    VerifyFunc->>HashEngine: hash_query_result_deterministic()
    HashEngine->>HashEngine: Sort result tuples
    HashEngine->>HashEngine: Serialize tuples
    HashEngine->>HashEngine: SHA-256 computation
    HashEngine-->>VerifyFunc: computed_hash
    VerifyFunc->>VerifyFunc: Copy to upper memory context
    VerifyFunc->>SPI: SPI_finish()

    Note over VerifyFunc: Step 4: Compare Hashes
    alt Hashes Match
        VerifyFunc->>VerifyFunc: stored_hash == computed_hash
        VerifyFunc->>Client: Verification PASSED - Return TRUE
    else Hashes Differ
        VerifyFunc->>VerifyFunc: stored_hash != computed_hash
        VerifyFunc->>Client: Verification FAILED - Return FALSE
    end
```

### Verification Decision Tree

```mermaid
flowchart TD
    A[verify_query_anchor] --> B{Anchor exists?}
    B -->|No| C[ERROR: Query anchor not found]
    B -->|Yes| D[Retrieve stored_query_text<br/>and stored_hash]

    D --> E[Re-execute stored_query_text]
    E --> F{Query executes?}
    F -->|Error| G[ERROR: Query execution failed]
    F -->|Success| H[Recompute hash from current result]

    H --> I{stored_hash ==<br/>computed_hash?}
    I -->|Yes| J[✓ VERIFICATION PASSED<br/>Data unchanged since anchor]
    I -->|No| K[✗ VERIFICATION FAILED<br/>Data modified or tampered]

    style C fill:#f44336,color:#fff
    style G fill:#f44336,color:#fff
    style J fill:#4caf50,color:#fff
    style K fill:#ff9800,color:#fff
```

**Verification Outcomes:**

| Result | Meaning | Action |
|--------|---------|--------|
| **TRUE** | Hashes match | Data integrity confirmed; query results unchanged since anchor creation |
| **FALSE** | Hashes differ | **Data tampering detected**; investigate source data modifications |
| **ERROR** | Query not found | Anchor does not exist; check `query_id` spelling |
| **ERROR** | Query execution failure | Source tables may be dropped; schema may have changed |

---

## Counter Management Flow

The global counter system provides monotonic, gap-tolerant sequencing for blockchain tables.

### Counter Allocation Flow

```mermaid
flowchart TD
    A[blockchainam_tuple_insert] --> B[BlockchainGetNextCounter table_oid]

    B --> C{Counter entry<br/>exists in shared<br/>memory?}

    C -->|No| D[Acquire ctl_lock EXCLUSIVE]
    D --> E{Check again<br/>double-check pattern}
    E -->|Still missing| F[Create BlockchainCounterEntry]
    F --> G[Lazy recovery:<br/>SELECT MAX __tx_lsn FROM table]
    G --> H[Initialize counter = MAX + 1]
    H --> I[Insert entry into counter_hash]
    I --> J[Release ctl_lock]

    C -->|Yes| K[Acquire per-table lock EXCLUSIVE]
    E -->|Found by another backend| K
    J --> K

    K --> L[Atomic increment:<br/>counter_value++]
    L --> M[Store new value in entry]
    M --> N[Release per-table lock]
    N --> O[Return counter value]

    style C fill:#fff9c4
    style G fill:#e3f2fd
    style L fill:#c8e6c9
    style O fill:#f3e5f5
```

### Shared Memory Counter Structure

```mermaid
graph TB
    subgraph SharedMemory [PostgreSQL Shared Memory]
        subgraph CounterData [BlockchainCounterData]
            A[ctl_lock: LWLock]
            B[counter_hash: HTAB*]
            C[hash_cache_lock: LWLock]
            D[hash_cache: HTAB*]
        end

        subgraph CounterHashTable [counter_hash: Per-Table Entries]
            E1[OID 16384 → BlockchainCounterEntry<br/>counter_value: 1523<br/>last_persisted: 1500<br/>lock: LWLock]
            E2[OID 16385 → BlockchainCounterEntry<br/>counter_value: 847<br/>last_persisted: 800<br/>lock: LWLock]
            E3[OID 16386 → BlockchainCounterEntry<br/>counter_value: 42<br/>last_persisted: 0<br/>lock: LWLock]
        end

        subgraph HashCacheTable [hash_cache: Uncommitted Hashes]
            H1[table_oid: 16384, counter: 1523<br/>→ hash_data: 0xabcd...]
            H2[table_oid: 16384, counter: 1522<br/>→ hash_data: 0x1234...]
            H3[table_oid: 16385, counter: 847<br/>→ hash_data: 0x5678...]
        end
    end

    B --> E1 & E2 & E3
    D --> H1 & H2 & H3

    style CounterData fill:#e3f2fd
    style CounterHashTable fill:#fff9c4
    style HashCacheTable fill:#c8e6c9
```

**Lock Hierarchy:**
- `ctl_lock` (global): Protects counter hash table structure (rare acquisition during table creation)
- `entry->lock` (per-table): Protects counter increment (frequent acquisition during INSERT)
- `hash_cache_lock` (global): Protects hash cache read/write (frequent acquisition)

**Design Rationale:**
- **Fine-grained locking** enables concurrent inserts to different blockchain tables
- **Per-table locks** serialize inserts to the same table (necessary for sequential counter)
- **Separate hash cache lock** allows hash lookups while counter is being incremented

---

## Hash Cache Lookup Flow

The hash cache is critical for concurrent performance, enabling sub-microsecond hash lookups for uncommitted blocks.

### Cache vs Database Lookup Decision Tree

```mermaid
flowchart TD
    A[Need prev_hash for counter N] --> B[Check Hash Cache:<br/>BlockchainGetCachedHash table_oid, N]

    B --> C{Found in<br/>cache?}

    C -->|Yes FAST PATH| D[Acquire hash_cache_lock SHARED]
    D --> E[memcpy hash_data to output]
    E --> F[Release hash_cache_lock]
    F --> G[Return prev_hash]

    C -->|No SLOW PATH| H[SPI_connect]
    H --> I[Execute:<br/>SELECT __curr_hash<br/>FROM table<br/>WHERE __tx_lsn = N]
    I --> J{Result found?}

    J -->|Yes| K[Copy bytea to output]
    K --> L[SPI_finish]
    L --> G

    J -->|No| M{Retry < 500?}
    M -->|Yes| N[pg_usleep 2ms]
    N --> B
    M -->|No| O[ERROR: Timeout<br/>waiting for prev_hash]

    style G fill:#4caf50,color:#fff
    style D fill:#e3f2fd
    style I fill:#fff9c4
    style O fill:#f44336,color:#fff
```

### Cache Performance Metrics

| Metric | Value | Notes |
|--------|-------|-------|
| **Cache Capacity** | 10,000 entries | Configurable via `MAX_CACHED_HASHES` |
| **Entry Size** | 48 bytes | `BlockchainHashEntry` structure |
| **Total Memory** | ~480 KB | For full cache |
| **Hit Latency** | <1 μs | Shared memory read with LWLock |
| **Miss Latency** | 2-50 ms | Database query via SPI |
| **Hit Rate** | >99% | In concurrent workloads (empirical) |
| **Eviction Policy** | None | Entries persist until server restart |

**Cache Storage Flow:**

```mermaid
sequenceDiagram
    participant Insert as blockchainam_tuple_insert
    participant Hash as Hash Computation
    participant Cache as Hash Cache

    Insert->>Hash: compute_curr_hash_with_counter(...)
    Hash->>Hash: bc_sha256_init()
    Hash->>Hash: bc_sha256_update(prev_hash)
    Hash->>Hash: bc_sha256_update(counter)
    Hash->>Hash: bc_sha256_update(timestamp)
    Hash->>Hash: bc_sha256_update(user_data)
    Hash->>Hash: bc_sha256_final() → curr_hash
    Hash-->>Insert: curr_hash (32 bytes)

    Note over Insert,Cache: IMMEDIATELY store in cache
    Insert->>Cache: BlockchainStoreHash(table_oid, counter, curr_hash)
    Cache->>Cache: Acquire hash_cache_lock EXCLUSIVE
    Cache->>Cache: Create BlockchainHashKey key = {table_oid, counter}
    Cache->>Cache: hash_search(HASH_ENTER)
    Cache->>Cache: memcpy(entry->hash_data, curr_hash, 32)
    Cache->>Cache: entry->valid = true
    Cache->>Cache: Release hash_cache_lock
    Cache-->>Insert: Success

    Note over Insert: Now available for next transaction
```

!!! info "Critical Timing"
    The hash is stored in the cache **immediately after computation** and **before heap insertion**. This ensures that the next concurrent transaction (counter+1) can find the hash in the cache while the current transaction is still writing to disk.

---

## Complete System Integration

This diagram shows how all components work together for a concurrent INSERT scenario.

```mermaid
sequenceDiagram
    participant T1 as Transaction 1 (counter=100)
    participant T2 as Transaction 2 (counter=101)
    participant Counter as Counter System
    participant Cache as Hash Cache
    participant Heap as Heap Storage

    Note over T1,T2: Both transactions start concurrently

    T1->>Counter: GetNextCounter()
    Counter-->>T1: counter = 100

    T2->>Counter: GetNextCounter()
    Counter-->>T2: counter = 101

    Note over T1: Retrieve prev_hash for counter 99
    T1->>Cache: GetCachedHash(oid, 99)
    Cache-->>T1: prev_hash_99 (found)

    Note over T2: Retrieve prev_hash for counter 100
    T2->>Cache: GetCachedHash(oid, 100)
    Cache-->>T2: NOT FOUND (T1 hasn't computed yet)

    Note over T1: Compute hash for block 100
    T1->>T1: SHA-256(data || prev_hash_99 || 100 || ts)
    T1->>T1: curr_hash_100

    Note over T1: CRITICAL: Store immediately
    T1->>Cache: StoreHash(oid, 100, curr_hash_100)
    Cache-->>T1: Stored

    Note over T2: Retry hash lookup
    T2->>Cache: GetCachedHash(oid, 100) [RETRY]
    Cache-->>T2: curr_hash_100 (FOUND!)

    Note over T2: Now can compute hash for block 101
    T2->>T2: SHA-256(data || curr_hash_100 || 101 || ts)
    T2->>T2: curr_hash_101

    T2->>Cache: StoreHash(oid, 101, curr_hash_101)
    Cache-->>T2: Stored

    par Insert to disk
        T1->>Heap: heap_insert(block_100)
        Heap-->>T1: Success
    and
        T2->>Heap: heap_insert(block_101)
        Heap-->>T2: Success
    end

    Note over T1,T2: Perfect hash chain: 99→100→101
```

**Key Observations:**
1. **Counter allocation serializes** (one at a time per table)
2. **Hash computation parallelizes** (different data)
3. **Cache enables cross-transaction visibility** (T2 sees T1's uncommitted hash)
4. **Retry loop handles timing** (T2 waits 2ms for T1's hash)
5. **Heap insertion parallelizes** (independent writes)

---

## Performance Comparison

### Before vs. After Concurrency Fix

| Metric | Before Fix (LSN-based) | After Fix (Counter-based) | Improvement |
|--------|------------------------|---------------------------|-------------|
| **Hash Chain Integrity** | 1.5% (20/1310 blocks) | 100% (1310/1310 blocks) | **98.5% → 100%** |
| **Genesis Blocks** | 3 (incorrect) | 1 (correct) | **Fixed** |
| **Fork Points** | 2 (branching) | 0 (linear) | **Fixed** |
| **Broken Links** | 1,290 (98.5% failure) | 0 (0% failure) | **100% success** |
| **Concurrent Throughput** | Unreliable | 881 TPS | **Stable** |
| **Cache Hit Rate** | N/A | >99% | **New feature** |

!!! success "Concurrency Achievement"
    The counter-based system with hash cache and retry loop achieves **100% hash chain integrity** under heavy concurrent load, resolving the critical race condition that caused 98.5% of concurrent inserts to create broken chains.

---

## Related Documentation

- [High-Level Design](high-level-design.md) - System architecture overview
- [Shared Memory Architecture](shared-memory.md) - Cache structure and concurrent access
- [Query Anchoring Architecture](query-anchoring-architecture.md) - Patent implementation details
- [API Reference](/api/functions.md) - Function signatures and usage
