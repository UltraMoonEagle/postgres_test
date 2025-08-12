# Architecture Overview

The PostgreSQL Blockchain Extension implements a sophisticated multi-layered architecture that transforms PostgreSQL into a blockchain-enabled database system. This section provides comprehensive technical documentation of all architectural components.

## System Architecture

```mermaid
graph TB
    subgraph "Application Layer"
        SQL[SQL Interface]
        Apps[Applications]
        Tools[Admin Tools]
    end
    
    subgraph "PostgreSQL Core + Blockchain Extension"
        subgraph "Parser Layer"
            SQLParser[SQL Parser]
            BlockchainGrammar[Blockchain Grammar Extensions]
        end
        
        subgraph "Query Processing"
            Planner[Query Planner]
            Executor[Query Executor]
            Hooks[Blockchain Hooks]
        end
        
        subgraph "Blockchain Components"
            TAM[Blockchain Table Access Method]
            HashEngine[Hash Chain Engine]
            CounterSystem[Global Counter System]
            ProtectionLayer[Immutability Protection]
            SystemColumns[System Column Manager]
        end
        
        subgraph "Storage Layer"
            Heap[Heap Storage]
            WAL[Write-Ahead Log]
            Buffer[Buffer Management]
        end
    end
    
    subgraph "Persistence"
        DataFiles[Data Files]
        CounterFiles[Counter Files]
        WALFiles[WAL Files]
    end
    
    Apps --> SQL
    Tools --> SQL
    SQL --> SQLParser
    SQLParser --> BlockchainGrammar
    BlockchainGrammar --> Planner
    Planner --> Executor
    Executor --> Hooks
    Hooks --> TAM
    
    TAM --> HashEngine
    TAM --> CounterSystem
    TAM --> ProtectionLayer
    TAM --> SystemColumns
    
    TAM --> Heap
    TAM --> WAL
    TAM --> Buffer
    
    Heap --> DataFiles
    WAL --> WALFiles
    CounterSystem --> CounterFiles
    
    style TAM fill:#e1f5fe,stroke:#01579b,stroke-width:3px
    style HashEngine fill:#f3e5f5,stroke:#4a148c,stroke-width:2px
    style CounterSystem fill:#e8f5e8,stroke:#1b5e20,stroke-width:2px
    style ProtectionLayer fill:#fff3e0,stroke:#e65100,stroke-width:2px
```

## Core Components

### 1. Blockchain Table Access Method (TAM)

The heart of the system that implements PostgreSQL's Table Access Method interface with blockchain-specific extensions.

<div class="grid cards" markdown>

-   **:material-database: Storage Engine**

    ---
    
    Custom table access method implementing all PostgreSQL TAM interfaces with immutability guarantees
    
    [:octicons-arrow-right-24: Learn more](blockchain-access-method.md)

-   **:material-table: System Columns**

    ---
    
    Automatic management of 9 blockchain metadata columns hidden from normal queries
    
    [:octicons-arrow-right-24: Details](blockchain-access-method.md#system-column-management)

</div>

**Key Features:**
- Full PostgreSQL TAM compliance
- Automatic system column injection
- Immutable tuple operations
- Seamless SQL integration

### 2. Hash Chain Engine

Cryptographic integrity system providing tamper-evident storage through SHA-256 hash chaining.

<div class="grid cards" markdown>

-   **:material-lock: Cryptographic Security**

    ---
    
    SHA-256 based hash chains linking every row to create tamper-evident storage
    
    [:octicons-arrow-right-24: Learn more](hash-chain-system.md)

-   **:material-link-variant: Chain Verification**

    ---
    
    Built-in verification mechanisms to detect any data tampering attempts
    
    [:octicons-arrow-right-24: Details](hash-chain-system.md#tamper-detection)

</div>

**Key Features:**
- SHA-256 cryptographic hashing
- Chain-based data linking
- Tamper detection capabilities
- Cross-platform consistency

### 3. Global Counter System

Sophisticated counter management providing unique sequential numbering across all blockchain tables.

<div class="grid cards" markdown>

-   **:material-counter: Unique Sequencing**

    ---
    
    Global counter system ensuring unique ordering across all blockchain operations
    
    [:octicons-arrow-right-24: Learn more](counter-management.md)

-   **:material-cached: Lazy Recovery**

    ---
    
    Intelligent recovery mechanism that eliminates startup delays while ensuring consistency
    
    [:octicons-arrow-right-24: Details](counter-management.md#lazy-recovery-mechanism)

</div>

**Key Features:**
- Shared memory architecture
- Lazy recovery mechanisms
- Atomic counter operations
- Persistent state management

### 4. Immutability Enforcement

Multi-layer protection system ensuring complete immutability through defense-in-depth approach.

<div class="grid cards" markdown>

-   **:material-shield-check: Multi-Layer Defense**

    ---
    
    Four-layer protection system preventing any data modification at every system level
    
    [:octicons-arrow-right-24: Learn more](immutability-enforcement.md)

-   **:material-security: Security Monitoring**

    ---
    
    Built-in monitoring and alerting for attempted security violations and attacks
    
    [:octicons-arrow-right-24: Details](immutability-enforcement.md#security-monitoring-and-auditing)

</div>

**Protection Layers:**
1. Parser-level syntax validation
2. Utility hook DDL interception
3. TAM-level operation blocking
4. System column access control

## Implementation Statistics

<div class="stats-grid">
<div class="stat-box">
<span class="stat-number">5,541</span>
<span class="stat-label">Lines of C Code</span>
</div>

<div class="stat-box">
<span class="stat-number">47</span>
<span class="stat-label">Implementation Commits</span>
</div>

<div class="stat-box">
<span class="stat-number">9</span>
<span class="stat-label">System Columns</span>
</div>

<div class="stat-box">
<span class="stat-number">4</span>
<span class="stat-label">Protection Layers</span>
</div>
</div>

## Component Interactions

### Insert Operation Flow

```mermaid
sequenceDiagram
    participant App as Application
    participant Parser as SQL Parser
    participant TAM as Blockchain TAM
    participant Hash as Hash Engine
    participant Counter as Counter System
    participant Storage as Storage Layer
    
    App->>Parser: INSERT INTO blockchain_table VALUES (...)
    Parser->>TAM: Validated insert operation
    
    TAM->>Counter: Get next counter value
    Counter-->>TAM: counter = N+1
    
    TAM->>Hash: Compute previous hash
    Hash-->>TAM: previous_hash
    
    TAM->>Hash: Compute current hash
    Hash-->>TAM: current_hash
    
    TAM->>TAM: Populate system columns
    TAM->>Storage: Store tuple with metadata
    Storage-->>TAM: Success
    TAM-->>App: INSERT successful
```

### Protection Layer Activation

```mermaid
graph LR
    subgraph "Blocked Operation Attempt"
        Attempt[UPDATE/DELETE/ALTER]
    end
    
    subgraph "Protection Layers"
        L1[Parser Check]
        L2[Utility Hook]
        L3[TAM Block]
        L4[Column Protection]
    end
    
    subgraph "Result"
        Error[ERROR: Operation Blocked]
        Log[Security Log Entry]
    end
    
    Attempt --> L1
    L1 --> L2
    L2 --> L3
    L3 --> L4
    L4 --> Error
    L4 --> Log
    
    style L1 fill:#ffebee
    style L2 fill:#fff3e0
    style L3 fill:#e8f5e8
    style L4 fill:#f3e5f5
    style Error fill:#ffcdd2
    style Log fill:#c8e6c9
```

## Performance Characteristics

### Computational Overhead

| Component | Overhead | Impact |
|-----------|----------|---------|
| **Hash Computation** | ~2µs per operation | Cryptographic security |
| **Counter Increment** | <1µs per operation | Unique sequencing |
| **System Columns** | ~2µs per operation | Metadata management |
| **Protection Checks** | <0.5µs per operation | Immutability guarantee |
| **Total Insert Overhead** | ~5% vs regular table | Complete blockchain functionality |

### Memory Usage

| Component | Memory Usage | Scalability |
|-----------|--------------|-------------|
| **Shared Memory Base** | 4KB fixed | O(1) |
| **Per-Table Counters** | 128 bytes each | O(n) tables |
| **Hash Computation** | 64 bytes temporary | O(1) per operation |
| **System Columns** | 104 bytes per row | O(1) per row |

## Integration Points

### PostgreSQL Core Integration

The blockchain extension integrates at multiple PostgreSQL subsystem levels:

```mermaid
graph TD
    subgraph "PostgreSQL Subsystems"
        Parser[Parser Subsystem]
        Planner[Planner Subsystem]
        Executor[Executor Subsystem]
        Storage[Storage Subsystem]
        Catalog[Catalog Subsystem]
        WAL[WAL Subsystem]
    end
    
    subgraph "Blockchain Integration Points"
        Grammar[Grammar Extensions]
        Hooks[Utility Hooks]
        TAMInterface[TAM Interface]
        SystemCatalog[System Catalog Extensions]
        WALLogging[WAL Logging Extensions]
    end
    
    Parser --> Grammar
    Executor --> Hooks
    Storage --> TAMInterface
    Catalog --> SystemCatalog
    WAL --> WALLogging
    
    style Grammar fill:#e3f2fd
    style Hooks fill:#f1f8e9
    style TAMInterface fill:#fce4ec
    style SystemCatalog fill:#fff8e1
    style WALLogging fill:#f3e5f5
```

### Extension Architecture

The blockchain functionality is implemented as a PostgreSQL extension with the following structure:

- **Core Library**: Shared library loaded by PostgreSQL
- **SQL Functions**: User-accessible blockchain operations
- **System Hooks**: Deep integration with PostgreSQL internals
- **Configuration**: Runtime configuration parameters

## Security Architecture

### Threat Model

The system protects against various attack vectors:

| Threat Category | Protection Mechanism | Implementation |
|-----------------|---------------------|----------------|
| **Data Tampering** | Cryptographic hashing | SHA-256 chain verification |
| **Unauthorized Modifications** | Multi-layer blocking | Parser + hooks + TAM |
| **Schema Changes** | DDL protection | Utility hook interception |
| **Privilege Escalation** | Access control | PostgreSQL permission system |
| **Replay Attacks** | Temporal binding | Timestamp inclusion in hash |
| **Reordering Attacks** | Sequential binding | Counter inclusion in hash |

### Defense in Depth

```mermaid
graph TB
    subgraph "Attack Vectors"
        SQLInjection[SQL Injection]
        DirectAccess[Direct File Access]
        PrivilegeEscalation[Privilege Escalation]
        SystemCompromise[System Compromise]
    end
    
    subgraph "Defense Layers"
        L1[Application Security]
        L2[SQL Parser Protection]
        L3[PostgreSQL Permissions]
        L4[Blockchain Immutability]
        L5[Cryptographic Integrity]
        L6[File System Security]
    end
    
    SQLInjection --> L1
    L1 --> L2
    DirectAccess --> L3
    L3 --> L4
    PrivilegeEscalation --> L4
    L4 --> L5
    SystemCompromise --> L5
    L5 --> L6
    
    style L1 fill:#ffebee
    style L2 fill:#fff3e0
    style L3 fill:#e8f5e8
    style L4 fill:#e1f5fe
    style L5 fill:#f3e5f5
    style L6 fill:#fce4ec
```

## Development Timeline

The architecture evolved through several phases:

### Phase 1: Foundation (Commits 1-15)
- Basic parser integration and relkind support
- Initial table access method skeleton
- Core system catalog integration

### Phase 2: Core Features (Commits 16-30)
- SHA-256 hash engine implementation
- System column management
- Basic insert operation pipeline

### Phase 3: Counter System (Commits 31-40)
- Global counter architecture
- Shared memory implementation
- Persistence and recovery mechanisms

### Phase 4: Production Ready (Commits 41-47)
- Performance optimizations
- Comprehensive protection layers
- Complete system integration

## Future Architecture Considerations

### Scalability Enhancements
- Partitioned counter systems for high-throughput scenarios
- Compressed storage for system columns
- Optimized hash computation algorithms

### Security Improvements
- Hardware security module integration
- Zero-knowledge proof support
- Quantum-resistant cryptographic algorithms

### Integration Expansions
- External blockchain network bridges
- Consensus mechanism implementations
- Advanced replication protocols

---

This architecture documentation provides the foundation for understanding how all blockchain components work together to provide immutable, tamper-evident storage within PostgreSQL while maintaining full SQL compatibility and performance.