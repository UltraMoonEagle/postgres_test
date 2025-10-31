# Financial Records Example

## Transaction Ledger

```sql
CREATE BLOCKCHAIN TABLE transaction_ledger (
    transaction_id UUID DEFAULT gen_random_uuid(),
    account_from VARCHAR(50) NOT NULL,
    account_to VARCHAR(50) NOT NULL,
    amount NUMERIC(19, 4) NOT NULL CHECK (amount > 0),
    currency VARCHAR(3) NOT NULL,
    transaction_type VARCHAR(20) NOT NULL,
    reference_number VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Insert transactions
INSERT INTO transaction_ledger (account_from, account_to, amount, currency, transaction_type)
VALUES 
    ('ACC-1001', 'ACC-2001', 1500.00, 'USD', 'transfer'),
    ('ACC-2001', 'ACC-3001', 750.50, 'USD', 'payment'),
    ('ACC-1001', 'ACC-3001', 2000.00, 'USD', 'transfer');

-- Query ledger with blockchain metadata
SELECT 
    account_from,
    account_to,
    amount,
    __tx_lsn AS sequence,
    __tx_timestamp AS timestamp,
    encode(__curr_hash, 'hex') AS hash
FROM transaction_ledger
ORDER BY __tx_lsn;
```

## Key Benefits

- **Immutability**: Transactions cannot be altered or deleted
- **Audit trail**: Complete history with cryptographic proof
- **Tamper detection**: Hash chain reveals any modifications
