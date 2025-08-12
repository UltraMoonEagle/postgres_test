# Quick Start Guide

Get up and running with PostgreSQL Blockchain Extension in minutes! This guide will walk you through creating your first blockchain table and understanding the basic concepts.

## Prerequisites

Before starting, ensure you have:

- [x] PostgreSQL 15+ installed with development headers
- [x] The Blockchain Extension compiled and installed
- [x] Superuser access to a PostgreSQL database
- [x] Basic familiarity with SQL

!!! tip "Installation Help"
    If you haven't installed the extension yet, see the [Installation Guide](../deployment/installation.md) for detailed instructions.

## Step 1: Enable the Extension

First, connect to your PostgreSQL database and enable the blockchain extension:

```sql
-- Connect to your database
\c your_database

-- Enable the blockchain extension (requires superuser)
CREATE EXTENSION IF NOT EXISTS blockchain_tables;

-- Verify the extension is loaded
\dx blockchain_tables
```

Expected output:
```
                          List of installed extensions
      Name       | Version |   Schema   |              Description
-----------------+---------+------------+----------------------------------------
 blockchain_tables| 1.0     | public     | PostgreSQL Blockchain Table Extension
```

## Step 2: Create Your First Blockchain Table

Let's create a simple audit log table:

```sql title="Creating an Audit Log Blockchain Table"
CREATE BLOCKCHAIN TABLE audit_log (
    user_id INTEGER NOT NULL,
    action VARCHAR(200) NOT NULL,
    resource_id INTEGER,
    ip_address INET,
    user_agent TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

!!! success "Table Created Successfully!"
    Your blockchain table is now ready. Notice that we use `CREATE BLOCKCHAIN TABLE` instead of the regular `CREATE TABLE` syntax.

## Step 3: Insert Some Data

Now let's add some audit entries:

```sql title="Inserting Data"
-- Insert audit entries
INSERT INTO audit_log (user_id, action, resource_id, ip_address, user_agent)
VALUES 
    (1001, 'login', NULL, '192.168.1.100', 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)'),
    (1001, 'view_document', 5432, '192.168.1.100', 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)'),
    (1002, 'login', NULL, '10.0.0.25', 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)'),
    (1001, 'edit_document', 5432, '192.168.1.100', 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)'),
    (1001, 'logout', NULL, '192.168.1.100', 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)');
```

## Step 4: Query Your Data

### Basic Queries

Query the data just like any regular table:

```sql title="Basic SELECT Query"
SELECT * FROM audit_log;
```

Expected output:
```
 user_id |     action      | resource_id |   ip_address   |                    user_agent                    |       created_at        
---------+-----------------+-------------+----------------+--------------------------------------------------+-------------------------
    1001 | login           |             | 192.168.1.100  | Mozilla/5.0 (Windows NT 10.0; Win64; x64)       | 2025-08-12 10:30:15.123
    1001 | view_document   |        5432 | 192.168.1.100  | Mozilla/5.0 (Windows NT 10.0; Win64; x64)       | 2025-08-12 10:30:15.124
    1002 | login           |             | 10.0.0.25      | Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) | 2025-08-12 10:30:15.125
    1001 | edit_document   |        5432 | 192.168.1.100  | Mozilla/5.0 (Windows NT 10.0; Win64; x64)       | 2025-08-12 10:30:15.126
    1001 | logout          |             | 192.168.1.100  | Mozilla/5.0 (Windows NT 10.0; Win64; x64)       | 2025-08-12 10:30:15.127
```

!!! info "System Columns Hidden"
    Notice that the system columns (prefixed with `__`) are automatically hidden from `SELECT *` queries. This keeps your regular queries clean while preserving all blockchain metadata.

### Accessing System Columns

To see the blockchain metadata, explicitly name the system columns:

```sql title="Viewing System Columns"
SELECT 
    user_id,
    action,
    __tx_lsn as sequence_number,
    __tx_timestamp as blockchain_timestamp,
    encode(__curr_hash, 'hex') as current_hash_hex
FROM audit_log
ORDER BY __tx_lsn;
```

Expected output:
```
 user_id |     action      | sequence_number |   blockchain_timestamp  |                      current_hash_hex                      
---------+-----------------+-----------------+-------------------------+------------------------------------------------------------
    1001 | login           |               1 | 2025-08-12 10:30:15.123 | a1b2c3d4e5f6789012345678901234567890abcdef1234567890abcdef
    1001 | view_document   |               2 | 2025-08-12 10:30:15.124 | b2c3d4e5f6789012345678901234567890abcdef1234567890abcdefa1
    1002 | login           |               3 | 2025-08-12 10:30:15.125 | c3d4e5f6789012345678901234567890abcdef1234567890abcdefa1b2
    1001 | edit_document   |               4 | 2025-08-12 10:30:15.126 | d4e5f6789012345678901234567890abcdef1234567890abcdefa1b2c3
    1001 | logout          |               5 | 2025-08-12 10:30:15.127 | e5f6789012345678901234567890abcdef1234567890abcdefa1b2c3d4
```

## Step 5: Test Immutability

One of the key features of blockchain tables is immutability. Let's verify that modification operations are blocked:

### Try to Update Data

```sql title="Testing UPDATE Prevention"
-- This will fail with an error
UPDATE audit_log SET action = 'modified_login' WHERE user_id = 1001;
```

Expected error:
```
ERROR:  UPDATE is not supported on blockchain tables
DETAIL:  Blockchain tables are immutable
HINT:  Use INSERT to add new versions of data
```

### Try to Delete Data

```sql title="Testing DELETE Prevention"
-- This will also fail with an error
DELETE FROM audit_log WHERE user_id = 1002;
```

Expected error:
```
ERROR:  DELETE is not supported on blockchain tables
DETAIL:  Blockchain tables are immutable
```

### Try to Truncate Table

```sql title="Testing TRUNCATE Prevention"
-- This will fail too
TRUNCATE audit_log;
```

Expected error:
```
ERROR:  TRUNCATE is not allowed on blockchain table "audit_log"
DETAIL:  Blockchain tables cannot be emptied
HINT:  Drop and recreate the table if you need to start over
```

!!! success "Immutability Verified!"
    As expected, all modification operations are blocked. This ensures your audit trail remains tamper-proof.

## Step 6: Using Helper Functions

The extension provides several SQL functions to work with blockchain tables:

### Check if a Table is a Blockchain Table

```sql title="Verify Table Type"
SELECT is_blockchain_table('audit_log');
-- Returns: true

SELECT is_blockchain_table('some_regular_table');
-- Returns: false
```

### List All Blockchain Tables

```sql title="List Blockchain Tables"
SELECT * FROM list_blockchain_tables();
```

Expected output:
```
 schema_name | table_name 
-------------+------------
 public      | audit_log
```

### Get Detailed Table Information

```sql title="Describe Blockchain Table Structure"
SELECT * FROM describe_blockchain_table('audit_log');
```

Expected output:
```
  column_name   |         data_type          | is_system_column |        description        
----------------+----------------------------+------------------+---------------------------
 user_id        | integer                    | f                | User-defined column
 action         | character varying(200)     | f                | User-defined column
 resource_id    | integer                    | f                | User-defined column
 ip_address     | inet                       | f                | User-defined column
 user_agent     | text                       | f                | User-defined column
 created_at     | timestamp without time zone| f                | User-defined column
 __row_id       | uuid                       | t                | Unique row identifier
 __curr_hash    | bytea                      | t                | SHA-256 hash of current row
 __prev_hash    | bytea                      | t                | Hash of previous row
 __tx_type      | text                       | t                | Transaction type
 __tx_lsn       | bigint                     | t                | Global counter value
 __tx_origin    | uuid                       | t                | Transaction origin
 __tx_version   | integer                    | t                | Row version
 __is_latest    | boolean                    | t                | Latest flag
 __tx_timestamp | timestamp with time zone   | t                | Transaction timestamp
```

## Step 7: Verify Chain Integrity

You can verify that the hash chain is intact and hasn't been tampered with:

```sql title="Verify Hash Chain Integrity"
SELECT * FROM verify_hash_chain('audit_log');
```

Expected output (all valid):
```
 counter_value | hash_valid | error_message 
---------------+------------+---------------
             1 | t          | 
             2 | t          | 
             3 | t          | 
             4 | t          | 
             5 | t          | 
```

## Common Patterns

### Pattern 1: Audit Trail with Context

```sql title="Comprehensive Audit Entry"
-- Function to insert audit entry with full context
CREATE OR REPLACE FUNCTION log_audit_event(
    p_user_id INTEGER,
    p_action VARCHAR(200),
    p_resource_id INTEGER DEFAULT NULL,
    p_details JSONB DEFAULT NULL
) RETURNS VOID AS $$
BEGIN
    INSERT INTO audit_log (
        user_id, 
        action, 
        resource_id, 
        ip_address,
        user_agent,
        details
    ) VALUES (
        p_user_id,
        p_action,
        p_resource_id,
        inet_client_addr(),
        current_setting('application_name', true),
        p_details
    );
END;
$$ LANGUAGE plpgsql;

-- Usage example
SELECT log_audit_event(1001, 'document_access', 5432, '{"document_type": "confidential", "access_level": "read"}');
```

### Pattern 2: Querying Recent Activity

```sql title="Recent Activity Query"
-- Get recent audit activity
SELECT 
    user_id,
    action,
    resource_id,
    ip_address,
    __tx_timestamp,
    __tx_lsn
FROM audit_log 
WHERE __tx_timestamp >= NOW() - INTERVAL '1 hour'
ORDER BY __tx_lsn DESC;
```

### Pattern 3: User Activity Summary

```sql title="User Activity Analysis"
-- Summarize user activity
SELECT 
    user_id,
    COUNT(*) as total_actions,
    COUNT(DISTINCT action) as unique_actions,
    MIN(__tx_timestamp) as first_activity,
    MAX(__tx_timestamp) as last_activity
FROM audit_log 
GROUP BY user_id
ORDER BY total_actions DESC;
```

## What's Next?

Congratulations! You've successfully:

- ✅ Created your first blockchain table
- ✅ Inserted immutable data
- ✅ Verified immutability protection
- ✅ Used helper functions
- ✅ Verified hash chain integrity

### Continue Your Journey

<div class="grid cards" markdown>

-   :material-book-open-page-variant:{ .lg .middle } **User Guide**

    ---

    Learn advanced features and best practices

    [:octicons-arrow-right-24: User Guide](../user-guide/)

-   :material-api:{ .lg .middle } **SQL Functions**

    ---

    Explore all available blockchain functions

    [:octicons-arrow-right-24: SQL Functions Reference](../api/sql-functions-reference.md)

-   :material-cogs:{ .lg .middle } **Configuration**

    ---

    Tune performance and configure options

    [:octicons-arrow-right-24: Configuration Guide](../deployment/configuration.md)

-   :material-lightbulb:{ .lg .middle } **Examples**

    ---

    See real-world usage examples

    [:octicons-arrow-right-24: Examples](../examples/)

</div>

## Troubleshooting

### Common Issues

!!! question "Extension not found?"
    
    **Problem**: `ERROR: extension "blockchain_tables" is not available`
    
    **Solution**: Make sure the extension is properly compiled and installed. Check the [Installation Guide](../deployment/installation.md).

!!! question "Permission denied?"
    
    **Problem**: `ERROR: permission denied to create extension`
    
    **Solution**: You need superuser privileges to install extensions. Connect as a superuser or ask your DBA.

!!! question "Syntax errors?"
    
    **Problem**: `ERROR: syntax error at or near "BLOCKCHAIN"`
    
    **Solution**: Make sure you're using PostgreSQL 15+ and the extension is properly loaded.

### Getting Help

If you encounter issues:

1. Check the [FAQ](../reference/faq.md)
2. Review the [Troubleshooting Guide](../deployment/troubleshooting.md)  
3. Search [GitHub Issues](https://github.com/your-org/apostgres/issues)
4. Open a new issue if needed

---

You're now ready to use PostgreSQL Blockchain Extension for immutable, tamper-evident data storage! 🎉