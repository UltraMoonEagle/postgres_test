# Glossary

**Blockchain Table**: PostgreSQL table using the blockchain table access method, providing immutability and hash chaining.

**Counter**: Monotonic sequence number (`__tx_lsn`) providing total ordering per table.

**Hash Chain**: Cryptographic linkage where each block's hash depends on the previous block's hash.

**Query Anchoring**: Storing a cryptographic hash of a query result set for later verification.

**System Columns**: Nine automatic columns (prefixed with `__`) added to every blockchain table.

**Table Access Method (TAM)**: PostgreSQL plugin interface for custom storage implementations.

For more terminology, see [Architecture](../architecture/index.md).
