# API Reference Overview

The PostgreSQL Blockchain Extension provides comprehensive APIs for both C-level development and SQL-level operations. This section contains detailed reference documentation for all public interfaces.

## API Categories

<div class="grid cards" markdown>

-   :material-code-braces:{ .lg .middle } **C API Reference**

    ---

    Complete reference for C functions, structures, and interfaces for extension developers

    [:octicons-arrow-right-24: C API Reference](c-api-reference.md)

-   :material-database:{ .lg .middle } **SQL Functions**

    ---

    User-facing SQL functions for blockchain table operations and management

    [:octicons-arrow-right-24: SQL Functions Reference](sql-functions-reference.md)

-   :material-table:{ .lg .middle } **System Catalogs**

    ---

    System catalog extensions and blockchain-specific metadata

    [:octicons-arrow-right-24: System Catalogs](system-catalogs.md)

</div>

## Quick Reference

### Core C Functions

```c
// Table Access Method
const TableAmRoutine *GetBlockchainTableAmRoutine(void);

// Hash Computation
bytea *compute_curr_hash_with_counter(Relation rel, TupleTableSlot *slot,
                                     TimestampTz ts, bytea *prev_hash, uint64 counter);

// Counter Management
uint64 BlockchainGetNextCounter(Oid table_oid);
void BlockchainCounterShmemInit(void);

// System Column Management
bool is_blockchain_system_column(const char *colname);
```

### Key SQL Functions

```sql
-- Table Management
SELECT is_blockchain_table('table_name');
SELECT * FROM list_blockchain_tables();
SELECT * FROM describe_blockchain_table('table_name');

-- System Column Access
SELECT * FROM get_blockchain_audit_data('table_name', 100);
SELECT * FROM get_blockchain_system_columns('table_name', row_uuid);

-- Chain Verification
SELECT * FROM verify_hash_chain('table_name');
SELECT * FROM get_hash_chain_info('table_name', counter_value);

-- Counter Management
SELECT * FROM get_blockchain_counters();
SELECT get_next_counter_preview('table_name');
```

## API Stability Levels

### Stable APIs
These APIs are considered stable and will maintain backward compatibility:

- **Core TAM Interface**: Table Access Method functions
- **Hash Functions**: SHA-256 computation and chain management
- **Counter System**: Basic counter operations
- **SQL Functions**: User-facing blockchain operations

### Evolving APIs
These APIs may change between versions with proper deprecation notices:

- **System Column Management**: Internal column handling
- **Utility Functions**: Administrative and maintenance functions
- **Advanced Features**: Performance optimization and monitoring

### Internal APIs
These APIs are for internal use and may change without notice:

- **Memory Management**: Internal memory allocation patterns
- **Low-level Storage**: Direct storage access functions
- **Debug Functions**: Development and debugging utilities

## Integration Guidelines

### C Extension Development

For developers creating C extensions that interact with blockchain tables:

1. **Include Headers**: Always include the appropriate blockchain headers
2. **Check Table Type**: Verify `relkind == RELKIND_BLOCKCHAIN_TABLE` before operations
3. **Respect Immutability**: Never attempt modification operations
4. **Use Provided APIs**: Use the documented APIs rather than direct access

### SQL Application Development

For application developers using SQL interfaces:

1. **Use Helper Functions**: Leverage provided SQL functions for blockchain operations
2. **Handle System Columns**: Be aware of hidden system columns in queries
3. **Error Handling**: Implement proper error handling for blocked operations
4. **Performance Considerations**: Understand the performance characteristics

## Error Handling

### Common Error Patterns

All APIs follow consistent error handling patterns:

```c
// C API Error Handling
PG_TRY();
{
    result = blockchain_operation(...);
}
PG_CATCH();
{
    // Handle blockchain-specific errors
    if (geterrcode() == ERRCODE_FEATURE_NOT_SUPPORTED)
    {
        // Handle immutability violation
    }
    PG_RE_THROW();
}
PG_END_TRY();
```

```sql
-- SQL Error Handling
DO $$
BEGIN
    PERFORM blockchain_operation('table_name');
EXCEPTION 
    WHEN feature_not_supported THEN
        RAISE NOTICE 'Operation blocked by immutability protection';
    WHEN others THEN
        RAISE;
END $$;
```

### Error Codes Reference

| Error Code | Meaning | Common Causes |
|------------|---------|---------------|
| `ERRCODE_FEATURE_NOT_SUPPORTED` | Operation not allowed | UPDATE/DELETE/ALTER on blockchain table |
| `ERRCODE_DATA_CORRUPTED` | Hash chain integrity failure | Data tampering detected |
| `ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE` | System not initialized | Counter system not ready |
| `ERRCODE_WRONG_OBJECT_TYPE` | Not a blockchain table | Function called on regular table |

## Performance Considerations

### API Performance Tiers

**High Performance** (< 1ms):
- `is_blockchain_table()`
- `BlockchainGetNextCounter()` (cached)
- Basic system column access

**Medium Performance** (1-10ms):
- Hash computation functions
- System column filtering
- Counter management operations

**Low Performance** (10ms+):
- Chain verification functions
- Comprehensive table analysis
- Export/import operations

### Optimization Guidelines

1. **Cache Results**: Cache results of expensive operations when possible
2. **Batch Operations**: Group related operations together
3. **Avoid Verification in Hot Paths**: Run verification during maintenance windows
4. **Use Appropriate APIs**: Choose the right API for your performance needs

## Version Compatibility

### API Versioning

The blockchain extension follows semantic versioning:

- **Major versions**: Breaking API changes
- **Minor versions**: New features with backward compatibility
- **Patch versions**: Bug fixes and performance improvements

### Migration Guide

When upgrading between versions:

1. **Check Release Notes**: Review API changes in release documentation
2. **Test Applications**: Verify application compatibility with new version
3. **Update Code**: Modify code to use new APIs where beneficial
4. **Performance Testing**: Validate performance with new version

## Examples and Tutorials

### Basic C Extension Example

```c
#include "blockchain/blockchainam.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(my_blockchain_function);

Datum
my_blockchain_function(PG_FUNCTION_ARGS)
{
    Oid table_oid = PG_GETARG_OID(0);
    Relation rel = relation_open(table_oid, AccessShareLock);
    
    // Check if this is a blockchain table
    if (rel->rd_rel->relkind != RELKIND_BLOCKCHAIN_TABLE)
    {
        relation_close(rel, AccessShareLock);
        ereport(ERROR, (errmsg("relation is not a blockchain table")));
    }
    
    // Perform blockchain-specific operations
    // ... implementation details ...
    
    relation_close(rel, AccessShareLock);
    PG_RETURN_BOOL(true);
}
```

### SQL Function Usage Example

```sql
-- Create a comprehensive blockchain audit function
CREATE OR REPLACE FUNCTION audit_user_actions(user_id_param INTEGER)
RETURNS TABLE(
    action TEXT,
    resource_id INTEGER,
    timestamp TIMESTAMPTZ,
    sequence_number BIGINT,
    hash_verified BOOLEAN
) AS $$
DECLARE
    table_name TEXT := 'user_audit_log';
    chain_valid BOOLEAN;
BEGIN
    -- Verify table is a blockchain table
    IF NOT is_blockchain_table(table_name) THEN
        RAISE EXCEPTION 'Table % is not a blockchain table', table_name;
    END IF;
    
    -- Return audit data with verification
    RETURN QUERY
    SELECT 
        a.action,
        a.resource_id,
        a.__tx_timestamp,
        a.__tx_lsn,
        v.hash_valid
    FROM user_audit_log a
    LEFT JOIN verify_hash_chain(table_name) v ON v.counter_value = a.__tx_lsn
    WHERE a.user_id = user_id_param
    ORDER BY a.__tx_lsn;
END;
$$ LANGUAGE plpgsql;
```

## Support and Community

### Getting Help

- **Documentation**: Check this API reference first
- **GitHub Issues**: Report bugs and request features
- **Discussions**: Ask questions and share ideas
- **Stack Overflow**: Community Q&A with `postgresql-blockchain` tag

### Contributing

- **Bug Reports**: Help improve API reliability
- **Feature Requests**: Suggest new API features
- **Documentation**: Improve API documentation
- **Code Contributions**: Extend API functionality

---

This API reference provides comprehensive coverage of all public interfaces in the PostgreSQL Blockchain Extension, enabling developers to effectively integrate with and extend the blockchain functionality.