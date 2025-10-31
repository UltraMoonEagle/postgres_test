# Troubleshooting Examples

## Common Scenarios

### Counter Gaps

Counter gaps are **expected** and occur when transactions abort:

```sql
SELECT 
    MIN(__tx_lsn) AS first,
    MAX(__tx_lsn) AS last,
    COUNT(*) AS rows,
    MAX(__tx_lsn) - MIN(__tx_lsn) + 1 - COUNT(*) AS gaps
FROM your_table;
```

Gaps indicate failed/rolled-back transactions and are part of the immutable audit trail.

### Performance Issues

If INSERT performance is slow:

1. Check if you're inserting one row at a time (use batch inserts)
2. Verify shared_buffers is adequate (min 128MB)
3. Check for excessive concurrent connections

### Hash Cache Misses

Rare but can happen under extreme concurrency. The retry loop (max 1 second) handles this automatically.

For more details, see:
- [Deployment: Troubleshooting](../deployment/troubleshooting.md)
- [Performance Testing](../testing/performance.md)
