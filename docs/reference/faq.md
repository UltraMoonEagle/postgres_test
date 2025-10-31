# Frequently Asked Questions

## General

**Q: Can I update or delete data in blockchain tables?**

A: No. Blockchain tables are append-only and immutable by design.

**Q: What are counter gaps?**

A: Gaps occur when transactions abort. They're expected and part of the audit trail.

**Q: Can I add indexes?**

A: Yes, blockchain tables support standard PostgreSQL indexes.

**Q: What's the performance impact?**

A: ~5-10% INSERT overhead compared to regular tables.

## For More Questions

See [User Guide](../user-guide/index.md) and [Examples](../examples/index.md).
