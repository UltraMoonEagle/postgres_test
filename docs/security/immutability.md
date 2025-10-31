# Immutability Guarantees

See [Immutability Enforcement](../architecture/immutability-enforcement.md) for technical details.

## Protection Layers

1. Parser-level blocking (UPDATE/DELETE methods set to NULL)
2. Utility hook interception (DDL commands)
3. Table Access Method enforcement
4. System catalog protections
