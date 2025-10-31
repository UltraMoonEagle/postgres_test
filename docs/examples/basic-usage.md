# Basic Usage Examples

For basic usage examples, see:

- [Your First Blockchain Table](../getting-started/first-table.md) - Complete tutorial
- [Quick Start Guide](../getting-started/quick-start.md) - Getting started

## Quick Example

```sql
-- Create blockchain table
CREATE BLOCKCHAIN TABLE demo (
    id SERIAL,
    data TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Insert data
INSERT INTO demo (data) VALUES ('Hello Blockchain!');

-- Query with system columns
SELECT id, data, __tx_lsn, __tx_timestamp FROM demo;
```
