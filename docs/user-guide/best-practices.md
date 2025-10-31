# Best Practices

## Design Patterns

### When to Use Blockchain Tables

✅ **Good use cases:**
- Audit logs (security, compliance)
- Financial transactions  
- Document access tracking
- Regulatory compliance data
- Supply chain events

❌ **Poor use cases:**
- Frequently updated data
- Temporary/staging tables
- High-churn data

## Performance Best Practices

1. **Batch inserts** when possible
2. **Use appropriate indexes** on query columns
3. **Archive old data** to separate tables if needed
4. **Monitor counter gaps** (expected from aborted transactions)

## Security Best Practices

1. Blockchain tables provide immutability, not access control
2. Use PostgreSQL roles and permissions for access control
3. Regularly verify chain integrity
4. Monitor for unusual counter gap patterns

For more details, see:
- [Getting Started Guide](../getting-started/quick-start.md)
- [Examples](../examples/index.md)
- [Security Guide](../security/index.md)
