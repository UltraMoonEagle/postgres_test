# Verification

For blockchain chain verification, see:

- [Chain Verification Example](../examples/verification.md) - Step-by-step verification guide
- [Testing: Chain Verification](../testing/chain-verification.md) - Advanced verification techniques

## Quick Start

```sql
-- Manual chain verification
WITH chain_check AS (
    SELECT
        __tx_lsn,
        __curr_hash,
        __prev_hash,
        LAG(__curr_hash) OVER (ORDER BY __tx_lsn) AS actual_prev_hash
    FROM your_blockchain_table
)
SELECT
    __tx_lsn,
    CASE
        WHEN __tx_lsn = 1 THEN 'Genesis block (OK)'
        WHEN __prev_hash = actual_prev_hash THEN 'Hash chain valid ✓'
        ELSE 'HASH CHAIN BROKEN! ✗'
    END AS chain_status
FROM chain_check
ORDER BY __tx_lsn;
```
