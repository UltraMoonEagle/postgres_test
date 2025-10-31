# Examples Overview

This section provides ready-to-run examples demonstrating all major features of the PostgreSQL Blockchain Extension. Each example includes complete SQL code, expected outputs, and explanations.

---

## Quick Navigation

<div class="grid cards" markdown>

-   :material-rocket-launch:{ .lg .middle } **Basic Usage**

    ---

    Create blockchain tables and perform CRUD operations

    [:octicons-arrow-right-24: View Example](basic-usage.md)

-   :material-account-multiple:{ .lg .middle } **Concurrent Insertions**

    ---

    Handle high-concurrency workloads with perfect chain integrity

    [:octicons-arrow-right-24: View Example](concurrent-inserts.md)

-   :material-anchor:{ .lg .middle } **Query Anchoring**

    ---

    Anchor arbitrary SQL query results for tamper detection

    [:octicons-arrow-right-24: View Example](query-anchoring.md)

-   :material-check-circle:{ .lg .middle } **Chain Verification**

    ---

    Verify blockchain integrity and detect tampering

    [:octicons-arrow-right-24: View Example](verification.md)

-   :material-security:{ .lg .middle } **Audit Logging**

    ---

    Immutable audit trails for compliance and security

    [:octicons-arrow-right-24: View Example](audit-logging.md)

-   :material-currency-usd:{ .lg .middle } **Financial Records**

    ---

    Tamper-proof financial transaction ledgers

    [:octicons-arrow-right-24: View Example](financial-records.md)

-   :material-bug:{ .lg .middle } **Troubleshooting**

    ---

    Debug common issues and error scenarios

    [:octicons-arrow-right-24: View Example](troubleshooting.md)

</div>

---

## Example Categories

### Beginner Examples
Perfect for getting started with blockchain tables:

1. **[Basic Usage](basic-usage.md)**: Create tables, insert data, query results
2. **[Chain Verification](verification.md)**: Verify hash chain integrity
3. **[Audit Logging](audit-logging.md)**: Simple immutable audit log

### Intermediate Examples
Explore advanced features:

4. **[Concurrent Insertions](concurrent-inserts.md)**: High-throughput concurrent workloads
5. **[Query Anchoring](query-anchoring.md)**: Anchor query results for verification
6. **[Financial Records](financial-records.md)**: Double-entry bookkeeping with blockchain

### Advanced Examples
Production-ready patterns:

7. **[Troubleshooting](troubleshooting.md)**: Debug and resolve common issues

---

## Running the Examples

### Prerequisites

1. **PostgreSQL with blockchain extension installed** (see [Installation Guide](../getting-started/installation.md))
2. **psql command-line tool** or any PostgreSQL client
3. **Basic SQL knowledge**

### Copy-Paste Ready

All examples are designed to be **copy-paste ready**. Simply:

1. Start `psql` connected to your database
2. Copy the SQL code from any example
3. Paste into psql and execute
4. Observe the output and explanations

### Example Template

Each example follows this structure:

````markdown
## Example Title

**Difficulty:** Beginner/Intermediate/Advanced
**Time:** X minutes
**Prerequisites:** List of requirements

### Scenario
Brief description of what this example demonstrates

### Code
```sql
-- Complete, runnable SQL code
```

### Expected Output
```
Sample output from running the code
```

### Explanation
Step-by-step walkthrough of what happened

### Key Takeaways
- Bullet points of important concepts
````

---

## Real-World Use Cases

### Compliance & Auditing
- **Healthcare (HIPAA)**: [Audit Logging](audit-logging.md) - Track patient record access
- **Finance (SOX)**: [Financial Records](financial-records.md) - Immutable transaction history
- **Government**: [Audit Logging](audit-logging.md) - Regulatory compliance trails

### Data Integrity
- **ML Datasets**: [Query Anchoring](query-anchoring.md) - Prove training data integrity
- **Supply Chain**: [Audit Logging](audit-logging.md) - Track product provenance
- **IoT Sensors**: [Concurrent Insertions](concurrent-inserts.md) - High-volume event logging

### Tamper Detection
- **Security Monitoring**: [Verification](verification.md) - Detect log tampering
- **Forensics**: [Chain Verification](verification.md) - Prove data timeline
- **Dispute Resolution**: [Financial Records](financial-records.md) - Irrefutable transaction proof

---

## Testing Environment Setup

### Quick Test Database

Create a dedicated database for testing examples:

```bash
# Create test database
createdb blockchain_examples

# Connect
psql blockchain_examples
```

### Sample Data Generator

Use this function to generate test data:

```sql
-- Function to generate random test data
CREATE OR REPLACE FUNCTION generate_test_data(table_name text, num_rows int)
RETURNS void AS $$
DECLARE
    i int;
BEGIN
    FOR i IN 1..num_rows LOOP
        EXECUTE format('
            INSERT INTO %I (user_id, action, details)
            VALUES ($1, $2, $3)',
            table_name
        ) USING
            (random() * 1000)::int,
            (ARRAY['login', 'logout', 'view', 'edit', 'delete'])[floor(random() * 5 + 1)],
            format('{"request_id": "%s", "timestamp": "%s"}',
                   gen_random_uuid(),
                   now()
            )::jsonb;
    END LOOP;
END;
$$ LANGUAGE plpgsql;
```

### Performance Test Utility

Benchmark blockchain table operations:

```sql
-- Function to measure INSERT performance
CREATE OR REPLACE FUNCTION benchmark_inserts(
    table_name text,
    num_inserts int,
    OUT elapsed_ms numeric,
    OUT rows_per_sec numeric
) AS $$
DECLARE
    start_time timestamptz;
    end_time timestamptz;
BEGIN
    start_time := clock_timestamp();

    PERFORM generate_test_data(table_name, num_inserts);

    end_time := clock_timestamp();
    elapsed_ms := EXTRACT(EPOCH FROM (end_time - start_time)) * 1000;
    rows_per_sec := num_inserts / (elapsed_ms / 1000.0);
END;
$$ LANGUAGE plpgsql;

-- Usage:
-- SELECT * FROM benchmark_inserts('my_blockchain_table', 1000);
```

---

## Example Data Sets

### Dataset 1: User Activity Log

```sql
CREATE BLOCKCHAIN TABLE user_activity (
    user_id INTEGER NOT NULL,
    action VARCHAR(50) NOT NULL,
    resource VARCHAR(200),
    ip_address INET,
    user_agent TEXT,
    session_id UUID,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

INSERT INTO user_activity (user_id, action, resource, ip_address) VALUES
    (101, 'login', '/auth/login', '192.168.1.100'),
    (101, 'view_dashboard', '/dashboard', '192.168.1.100'),
    (102, 'login', '/auth/login', '10.0.0.50'),
    (101, 'edit_profile', '/profile/edit', '192.168.1.100'),
    (103, 'login', '/auth/login', '172.16.0.10'),
    (102, 'view_reports', '/reports', '10.0.0.50'),
    (101, 'logout', '/auth/logout', '192.168.1.100');
```

### Dataset 2: Financial Transactions

```sql
CREATE BLOCKCHAIN TABLE transactions (
    transaction_id UUID DEFAULT gen_random_uuid(),
    from_account VARCHAR(20) NOT NULL,
    to_account VARCHAR(20) NOT NULL,
    amount NUMERIC(15, 2) NOT NULL,
    currency VARCHAR(3) DEFAULT 'USD',
    transaction_type VARCHAR(20),
    reference VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

INSERT INTO transactions (from_account, to_account, amount, transaction_type) VALUES
    ('ACC-1001', 'ACC-2001', 1500.00, 'transfer'),
    ('ACC-2001', 'ACC-3001', 750.50, 'payment'),
    ('ACC-1001', 'ACC-3001', 2000.00, 'transfer'),
    ('ACC-3001', 'ACC-1001', 500.00, 'refund'),
    ('ACC-2001', 'ACC-1001', 1200.75, 'payment');
```

### Dataset 3: Document Access

```sql
CREATE BLOCKCHAIN TABLE document_access (
    document_id VARCHAR(50) NOT NULL,
    document_type VARCHAR(30) NOT NULL,
    user_id INTEGER NOT NULL,
    access_type VARCHAR(20) NOT NULL,
    ip_address INET,
    organization_id INTEGER,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

INSERT INTO document_access (document_id, document_type, user_id, access_type, ip_address) VALUES
    ('DOC-2024-001', 'financial_statement', 101, 'view', '192.168.1.100'),
    ('DOC-2024-001', 'financial_statement', 101, 'download', '192.168.1.100'),
    ('DOC-2024-002', 'contract', 102, 'view', '10.0.0.50'),
    ('DOC-2024-003', 'invoice', 103, 'view', '172.16.0.10'),
    ('DOC-2024-001', 'financial_statement', 104, 'print', '192.168.1.105');
```

---

## Performance Expectations

Based on typical hardware (4-core CPU, 8GB RAM, SSD):

| Operation | Throughput | Latency |
|-----------|------------|---------|
| Simple INSERT | 30,000-50,000 rows/sec | 20-30 µs |
| Batch INSERT (1000 rows) | 40,000-60,000 rows/sec | 16-25 µs per row |
| SELECT with system columns | Same as regular tables | No overhead |
| Hash chain verification | 50,000-100,000 rows/sec | ~10-20 µs per row |
| Query anchoring (100 rows) | ~5-10 anchors/sec | ~100-200 ms |
| Query verification | ~5-10 verifications/sec | ~100-200 ms |

---

## Best Practices from Examples

After working through these examples, you'll learn:

1. **When to use blockchain tables** (immutable audit data, compliance records)
2. **How to handle concurrent inserts** (batching, connection pooling)
3. **Verification strategies** (periodic checks, real-time monitoring)
4. **Query anchoring patterns** (scheduled snapshots, ML dataset tracking)
5. **Performance optimization** (indexes, batching, partitioning)
6. **Troubleshooting techniques** (gap analysis, chain verification, log inspection)

---

## Next Steps

1. **Start with [Basic Usage](basic-usage.md)** to learn fundamentals
2. **Try [Concurrent Insertions](concurrent-inserts.md)** to test performance
3. **Explore [Query Anchoring](query-anchoring.md)** for advanced integrity
4. **Review [Troubleshooting](troubleshooting.md)** for production readiness
5. **Read [User Guide](../user-guide/index.md)** for comprehensive documentation

---

## Contributing Examples

Have a useful example to share? We'd love to include it!

See our [Contributing Guide](../development/contributing.md) for:
- Example submission process
- Code style guidelines
- Documentation standards
- Review process

**Example submission checklist:**
- [ ] Complete, runnable SQL code
- [ ] Expected output documented
- [ ] Clear explanation of concepts
- [ ] Real-world use case demonstrated
- [ ] Performance characteristics noted
- [ ] Error handling shown
- [ ] Best practices highlighted

---

## Additional Resources

- **[User Guide](../user-guide/index.md)**: Comprehensive feature documentation
- **[Architecture](../architecture/index.md)**: System design and internals
- **[API Reference](../api/index.md)**: Complete function reference
- **[Testing Guide](../testing/index.md)**: Test strategies and verification
- **[FAQ](../reference/faq.md)**: Frequently asked questions

