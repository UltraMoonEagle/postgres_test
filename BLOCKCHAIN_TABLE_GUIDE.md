# PostgreSQL Blockchain Table - Comprehensive Guide

## Overview

Blockchain tables in PostgreSQL provide an immutable, tamper-evident storage mechanism using cryptographic hash chaining and a global counter system. Each row is cryptographically linked to the previous row, creating an unbreakable chain of data integrity.

## Key Features

### 1. **Immutability**
- Only INSERT operations are allowed
- UPDATE, DELETE, and TRUNCATE are blocked
- ALTER TABLE operations (ADD/DROP COLUMN, ALTER TYPE) are prohibited
- Table structure cannot be modified after creation

### 2. **System Column Management**
- System columns (prefixed with `__`) are hidden from `SELECT *` queries
- System columns can be explicitly accessed when needed
- User columns can be renamed, but system columns cannot

### 3. **Cryptographic Integrity**
- Each row contains a SHA-256 hash of its data
- Hash chain links each row to the previous one
- First row has a zero hash as its previous hash
- Global counter ensures unique sequencing

### 4. **Counter-Based System**
- Replaces LSN (Log Sequence Number) approach
- Eliminates double-insert problem
- Persistent across server restarts
- Automatic recovery from table data

## System Columns

Every blockchain table automatically includes these system columns:

| Column | Type | Description |
|--------|------|-------------|
| `__row_id` | UUID | Unique identifier for each row |
| `__curr_hash` | BYTEA | SHA-256 hash of current row |
| `__prev_hash` | BYTEA | Hash of previous row in chain |
| `__tx_type` | TEXT | Transaction type (always 'INSERT' for blockchain tables) |
| `__tx_lsn` | BIGINT | Global counter value (sequence number) |
| `__tx_origin` | UUID | Transaction origin identifier |
| `__tx_version` | INTEGER | Row version number |
| `__is_latest` | BOOLEAN | Always true for blockchain tables |
| `__tx_timestamp` | TIMESTAMPTZ | Transaction timestamp |

## Creating a Blockchain Table

```sql
CREATE BLOCKCHAIN TABLE table_name (
    column1 datatype,
    column2 datatype,
    ...
);
```

**Important Notes:**
- Cannot use SERIAL or sequences (they require indexes)
- Cannot create PRIMARY KEY or any indexes
- Cannot use FOREIGN KEY constraints
- Table inherits all blockchain system columns automatically

## DDL/DML Permissions

### ✅ Allowed Operations
- `INSERT` - Add new records
- `SELECT` - Query data
- `ALTER TABLE ... RENAME TO` - Rename the table
- `ALTER TABLE ... RENAME COLUMN` - Rename user columns only

### ❌ Blocked Operations
- `UPDATE` - Cannot modify existing records
- `DELETE` - Cannot remove records
- `TRUNCATE` - Cannot empty the table
- `ALTER TABLE ... ADD COLUMN` - Cannot add columns
- `ALTER TABLE ... DROP COLUMN` - Cannot remove columns
- `ALTER TABLE ... ALTER COLUMN TYPE` - Cannot change column types
- `CREATE INDEX` - Cannot create indexes
- Renaming system columns (those starting with `__`)

## Helper Functions

### 1. Check if a table is a blockchain table
```sql
SELECT is_blockchain_table('table_name');
```

### 2. Describe blockchain table structure
```sql
SELECT * FROM describe_blockchain_table('table_name');
```

### 3. Access blockchain audit data
```sql
-- Direct access to system columns
SELECT column1, column2, __row_id, __curr_hash, __tx_lsn, __tx_timestamp 
FROM blockchain_table_name;
```

### 4. View all system columns
```sql
-- Access specific system columns
SELECT __row_id, __curr_hash, __prev_hash, __tx_lsn, __tx_type, __tx_timestamp
FROM blockchain_table_name;
```

## Counter Persistence

The global counter system provides:
- **Automatic persistence** to disk every 100 operations
- **Crash recovery** through lazy loading from table data
- **No startup delays** - counters recovered on first use
- **File location**: `$PGDATA/pg_blockchain_counters`

## Best Practices

1. **Design carefully** - Table structure cannot be changed after creation
2. **Plan for growth** - Only INSERTs are allowed, data accumulates forever
3. **Use appropriate data types** - Choose types that won't need modification
4. **Avoid indexes** - Design queries that work without indexes
5. **Monitor growth** - Blockchain tables only grow, never shrink

## Security Considerations

1. **Hash verification** - Can be implemented to verify chain integrity
2. **Tamper evidence** - Any modification breaks the hash chain
3. **Audit trail** - Complete history with timestamps
4. **Access control** - Use PostgreSQL roles and permissions

## Example Use Cases

1. **Audit Logs** - Immutable record of system events
2. **Financial Transactions** - Tamper-proof transaction history
3. **Compliance Records** - Regulatory data that must not be modified
4. **Supply Chain** - Track product journey without alterations
5. **Medical Records** - Patient data with full history

## Limitations

1. **No indexes** - May impact query performance on large tables
2. **No updates** - Corrections require new inserts
3. **No deletions** - GDPR compliance may require alternative approaches
4. **Storage growth** - Tables only grow, plan accordingly
5. **No partitioning** - Cannot partition blockchain tables