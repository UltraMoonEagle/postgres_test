# Threat Model

## Protections

- ✅ Unauthorized data modification (blocked by immutability)
- ✅ Data deletion (blocked)
- ✅ Backdating (timestamps and counters)
- ✅ Tampering detection (hash chain verification)

## Out of Scope

- ❌ Access control (use PostgreSQL roles)
- ❌ Encryption at rest (use PostgreSQL encryption)
- ❌ Network security (use SSL/TLS)
