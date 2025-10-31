# User Guide

Welcome to the PostgreSQL Blockchain Extension User Guide! This comprehensive guide will help you master blockchain tables, from basic operations to advanced query anchoring techniques.

## One-Minute Summary

!!! abstract "Quick Overview"
    - **What**: PostgreSQL Blockchain Extension adds immutable, cryptographically verified tables to standard PostgreSQL
    - **Why**: Ensure data integrity for audit trails, compliance, and tamper-evident record keeping
    - **How**: Use `CREATE BLOCKCHAIN TABLE` syntax and work with data like normal PostgreSQL tables

---

## Guide Sections

<div class="grid cards" markdown>

-   :material-database-import:{ .lg .middle } **Inserting Data**

    ---

    Learn how to insert data into blockchain tables using single inserts, batch operations, and INSERT...SELECT patterns.

    [:octicons-arrow-right-24: Inserting Data](inserting-data.md)

-   :material-database-search:{ .lg .middle } **Querying Data**

    ---

    Master querying blockchain tables, accessing system columns, filtering, and performing joins with both blockchain and regular tables.

    [:octicons-arrow-right-24: Querying Data](querying-data.md)

-   :material-anchor:{ .lg .middle } **Query Anchoring**

    ---

    Anchor arbitrary SQL query results to the blockchain for regulatory compliance, ML dataset provenance, and audit trails.

    [:octicons-arrow-right-24: Query Anchoring](query-anchoring.md)

-   :material-shield-check:{ .lg .middle } **Verification**

    ---

    Verify hash chain integrity, detect tampering, interpret verification results, and ensure data authenticity.

    [:octicons-arrow-right-24: Verification](verification.md)

</div>

---

## Common Use Cases

### Financial Audit Trails
Track all financial transactions in an immutable ledger that meets regulatory compliance requirements (SOX, GDPR, HIPAA).

```sql
CREATE BLOCKCHAIN TABLE financial_ledger (
    transaction_id BIGSERIAL PRIMARY KEY,
    account_from TEXT NOT NULL,
    account_to TEXT NOT NULL,
    amount NUMERIC(15,2) NOT NULL,
    transaction_type TEXT NOT NULL,
    description TEXT
);
```

### Access Control Logging
Maintain tamper-proof logs of who accessed what resources and when.

```sql
CREATE BLOCKCHAIN TABLE access_log (
    user_id INTEGER NOT NULL,
    resource_id INTEGER NOT NULL,
    action VARCHAR(50) NOT NULL,
    ip_address INET,
    user_agent TEXT
);
```

### Clinical Trial Data
Lock database records for FDA compliance, ensuring trial data cannot be modified after database lock.

```sql
CREATE BLOCKCHAIN TABLE trial_observations (
    patient_id TEXT NOT NULL,
    observation_date DATE NOT NULL,
    vital_signs JSONB,
    adverse_events TEXT[],
    investigator_notes TEXT
);
```

### Supply Chain Tracking
Track inventory movements and shipments with guaranteed non-repudiation.

```sql
CREATE BLOCKCHAIN TABLE inventory_movements (
    product_id TEXT NOT NULL,
    from_location TEXT,
    to_location TEXT,
    quantity INTEGER NOT NULL,
    movement_type VARCHAR(50) NOT NULL,
    carrier TEXT,
    tracking_number TEXT
);
```

---

## Key Features Overview

### Automatic System Columns

Every blockchain table automatically includes 9 system metadata columns:

| Column | Purpose |
|--------|---------|
| `__row_id` | Unique UUID identifier for each row |
| `__curr_hash` | SHA-256 hash of current block (32 bytes) |
| `__prev_hash` | Hash of previous block (chain linkage) |
| `__tx_type` | Transaction type (always "INSERT") |
| `__tx_lsn` | Global monotonic counter (total ordering) |
| `__tx_origin` | Transaction origin UUID |
| `__tx_version` | Row version number |
| `__is_latest` | Latest version flag |
| `__tx_timestamp` | Transaction timestamp with microsecond precision |

!!! tip "System Columns are Optional in Queries"
    System columns are hidden from `SELECT *` queries but can be accessed by explicitly naming them.

### Immutability Protection

Blockchain tables automatically block:

- **UPDATE** operations - Data cannot be modified
- **DELETE** operations - Data cannot be removed
- **TRUNCATE** operations - Table cannot be emptied
- **ALTER TABLE** (destructive changes) - Schema cannot be modified in ways that break integrity

### Cryptographic Hash Chains

Each row's hash is computed from:

1. Previous block's hash (chain linkage)
2. Counter value (total ordering)
3. Timestamp (temporal anchoring)
4. All user-defined column values (data integrity)

This creates an unbreakable chain where tampering with any row invalidates all subsequent rows.

---

## Performance Characteristics

| Metric | Value | Notes |
|--------|-------|-------|
| **Insert Overhead** | 6% | Compared to regular PostgreSQL tables |
| **Concurrent Throughput** | 881 TPS | 10 concurrent clients validated |
| **Hash Computation** | ~2 microseconds | Per row, including serialization |
| **Storage Overhead** | 135 bytes/row | Fixed cost for system metadata |
| **Chain Verification** | O(N) | Linear in number of rows |

!!! info "Concurrency Note"
    Inserts to the same blockchain table are serialized by counter allocation. Inserts to different blockchain tables are fully parallelized.

---

## Best Practices

### Schema Design

1. **Include business keys** - Don't rely solely on `__row_id` for lookups
2. **Add timestamps** - While `__tx_timestamp` exists, add application-level timestamps for clarity
3. **Use appropriate data types** - Smaller types reduce hash computation time
4. **Consider indexes** - Standard PostgreSQL indexes work on blockchain tables for query performance

### Application Integration

1. **Handle immutability gracefully** - Design UIs to show history, not edit records
2. **Use application-level soft deletes** - Add `is_deleted BOOLEAN` column instead of DELETE
3. **Implement versioning** - Insert new rows for updates, keep old versions
4. **Batch inserts when possible** - Use multi-row INSERT statements for better throughput

### Security Considerations

1. **Restrict table access** - Use PostgreSQL RBAC to control who can INSERT
2. **Monitor for tampering attempts** - Log blocked UPDATE/DELETE operations
3. **Verify chains periodically** - Schedule integrity checks during off-peak hours
4. **Archive old data** - Export verified chains to cold storage for long-term retention

---

## What's Next?

Ready to dive deeper? Start with these essential guides:

!!! example "Getting Started Path"
    1. **[Inserting Data](inserting-data.md)** - Learn the basics of adding records
    2. **[Querying Data](querying-data.md)** - Master data retrieval techniques
    3. **[Query Anchoring](query-anchoring.md)** - Anchor complex query results
    4. **[Verification](verification.md)** - Ensure data integrity

---

## Getting Help

!!! question "Need Assistance?"
    - **Documentation**: Browse the [API Reference](../api/sql-functions-reference.md)
    - **Architecture**: Understand the [internals](../architecture/index.md)
    - **Examples**: See [real-world examples](../examples/index.md)
    - **GitHub Issues**: Report bugs or request features

---

*This user guide is part of the PostgreSQL Blockchain Extension documentation. For installation instructions, see the [Quick Start Guide](../getting-started/quick-start.md).*
