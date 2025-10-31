# Query Anchoring - Anchoring SQL Query Results

Learn how to anchor arbitrary SQL query results to blockchain tables for regulatory compliance, ML dataset provenance, and tamper-evident analytics.

## One-Minute Summary

!!! abstract "Key Takeaways"
    - **Anchor ANY SQL query** - SELECT, JOIN, aggregations, CTEs all supported
    - **Deterministic hashing** ensures reproducible results regardless of execution order
    - **Verify query integrity** by re-executing and comparing cryptographic hashes
    - **Use cases**: Regulatory reports, ML datasets, financial reconciliation, audit snapshots

---

## What is Query Anchoring?

Query anchoring extends blockchain integrity guarantees from individual table rows to **entire query result sets**. This enables you to:

1. **Prove query results haven't changed** - Cryptographic hash anchors the result
2. **Detect data tampering** - Re-execute query and compare hashes
3. **Create audit trails** - Store queries with metadata (who, when, why)
4. **Ensure reproducibility** - ML datasets, financial reports remain verifiable

### How It Works

```
┌─────────────────────────────────────────────────┐
│  1. Execute SQL Query                           │
│     SELECT SUM(amount) FROM sales WHERE year=2024│
└─────────────────┬───────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────┐
│  2. Sort Results Deterministically              │
│     • Column-by-column comparison               │
│     • Consistent NULL ordering                  │
└─────────────────┬───────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────┐
│  3. Serialize Each Row                          │
│     "col1|col2|col3|..."                        │
└─────────────────┬───────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────┐
│  4. Compute SHA-256 Hash                        │
│     hash(query_text + sorted_rows)              │
└─────────────────┬───────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────┐
│  5. Store in Blockchain Anchor Table            │
│     (query_id, query_text, hash, timestamp)     │
└─────────────────────────────────────────────────┘
```

---

## Step 1: Create an Anchor Table

First, create a blockchain table to store query anchors.

### Using Helper Function

```sql
-- Create anchor table (recommended)
SELECT create_anchor_table('query_anchors');
```

**Output:**
```
        create_anchor_table
-----------------------------------
 Anchor table created successfully
```

This creates a blockchain table with the following schema:

```sql
CREATE BLOCKCHAIN TABLE query_anchors (
    query_id TEXT NOT NULL,              -- Your identifier
    query_text TEXT NOT NULL,            -- SQL query being anchored
    result_hash BYTEA NOT NULL,          -- SHA-256 hash (32 bytes)
    anchor_time TIMESTAMPTZ NOT NULL,    -- When anchor was created
    user_id TEXT NOT NULL,               -- Who created the anchor
    notes TEXT                           -- Optional metadata
);
```

### Manual Creation

Alternatively, create the table manually for custom schema:

```sql
CREATE BLOCKCHAIN TABLE compliance_anchors (
    query_id TEXT NOT NULL,
    department TEXT NOT NULL,           -- Custom field
    query_text TEXT NOT NULL,
    result_hash BYTEA NOT NULL,
    compliance_standard TEXT,           -- Custom field (e.g., "SOX", "HIPAA")
    anchor_time TIMESTAMPTZ NOT NULL,
    user_id TEXT NOT NULL,
    notes TEXT
);
```

---

## Step 2: Anchor a Query Result

Use `anchor_query_result()` to anchor any SQL query to your blockchain table.

### Basic Anchoring

```sql
SELECT anchor_query_result(
    'query_anchors',                    -- Anchor table name
    'monthly_sales_2024_10',            -- Your unique query ID
    'SELECT product_id, SUM(amount) as total
     FROM sales
     WHERE EXTRACT(YEAR FROM sale_date) = 2024
       AND EXTRACT(MONTH FROM sale_date) = 10
     GROUP BY product_id
     ORDER BY product_id',              -- Query to anchor
    'Monthly sales summary for Oct 2024 compliance report'  -- Optional notes
);
```

**Output:**
```
INFO:  Anchoring query: monthly_sales_2024_10
INFO:  Query anchor stored successfully with hash: \x3abb974851d8201b66e8222a4ee6af14cb08095672d248469128ebf1e0ef68aa

                        anchor_query_result
--------------------------------------------------------------------
 \x3abb974851d8201b66e8222a4ee6af14cb08095672d248469128ebf1e0ef68aa
```

!!! success "Query Anchored!"
    The 32-byte SHA-256 hash uniquely identifies this exact query result. Any change to the underlying data will produce a different hash.

### Function Signature

```sql
anchor_query_result(
    anchor_table TEXT,     -- Name of blockchain anchor table
    query_id TEXT,         -- Unique identifier for this query
    query_text TEXT,       -- SQL query to execute and anchor
    notes TEXT DEFAULT ''  -- Optional metadata/description
) RETURNS BYTEA           -- Returns SHA-256 hash (32 bytes)
```

---

## Step 3: View Anchored Queries

Query the anchor table to see all stored query anchors.

### List All Anchors

```sql
SELECT
    query_id,
    encode(result_hash, 'hex') as hash_hex,
    anchor_time,
    user_id,
    notes
FROM query_anchors
ORDER BY anchor_time DESC;
```

**Output:**
```
      query_id       |                             hash_hex                              |       anchor_time        | user_id |              notes
---------------------+-------------------------------------------------------------------+--------------------------+---------+-----------------------------------
 monthly_sales_2024_10| 3abb974851d8201b66e8222a4ee6af14cb08095672d248469128ebf1e0ef68aa | 2025-10-31 14:00:00+00   | alice   | Monthly sales summary for Oct 2024
 customer_list_q3    | a1b2c3d4e5f6789012345678901234567890abcdef1234567890abcdef12345678 | 2025-10-30 10:30:00+00   | bob     | Q3 customer snapshot for audit
```

### Query Details

```sql
-- Get specific anchor details including the stored query
SELECT
    query_id,
    query_text,
    encode(result_hash, 'hex') as proof_hash,
    anchor_time,
    notes,
    __tx_lsn as blockchain_sequence,
    encode(__curr_hash, 'hex') as blockchain_hash
FROM query_anchors
WHERE query_id = 'monthly_sales_2024_10';
```

---

## Step 4: Verify Query Anchor

Verify that the query result hasn't been tampered with by re-executing and comparing hashes.

### Basic Verification

```sql
SELECT verify_query_anchor(
    'query_anchors',           -- Anchor table name
    'monthly_sales_2024_10'    -- Query ID to verify
);
```

### Successful Verification

**Output (data unchanged):**
```
INFO:  Verifying query anchor: monthly_sales_2024_10
INFO:  ✓ Verification PASSED: Query result matches anchored hash

 verify_query_anchor
---------------------
 t
(1 row)
```

!!! success "Verification Passed"
    The returned `t` (true) indicates the current query result produces the exact same hash as when originally anchored. Data integrity confirmed!

### Failed Verification (Tampering Detected)

**Output (data modified):**
```
INFO:  Verifying query anchor: monthly_sales_2024_10
WARNING:  ✗ Verification FAILED: Hash mismatch detected
WARNING:  This indicates tampering or data modification since anchor was created

 verify_query_anchor
---------------------
 f
(1 row)
```

!!! danger "Tampering Detected"
    The returned `f` (false) indicates the data has changed since the anchor was created. This could indicate:

    - Malicious tampering
    - Accidental data modification
    - Legitimate updates (re-anchor if expected)

### Function Signature

```sql
verify_query_anchor(
    anchor_table TEXT,   -- Name of blockchain anchor table
    query_id TEXT        -- Query ID to verify
) RETURNS BOOLEAN       -- TRUE if verified, FALSE if tampered
```

---

## Advanced Query Anchoring

### Anchoring Complex Queries

Query anchoring supports ALL SQL features:

=== "Multi-Table Joins"

    ```sql
    SELECT anchor_query_result(
        'compliance_anchors',
        'customer_txn_report_2024_q4',
        'SELECT
            c.customer_id,
            c.name,
            c.email,
            t.transaction_date,
            t.amount,
            t.category,
            t.status
        FROM customers c
        JOIN transactions t ON c.customer_id = t.customer_id
        WHERE EXTRACT(YEAR FROM t.transaction_date) = 2024
          AND EXTRACT(QUARTER FROM t.transaction_date) = 4
        ORDER BY c.customer_id, t.transaction_date',
        'Q4 2024 customer transaction report for SOX compliance'
    );
    ```

=== "Aggregations"

    ```sql
    SELECT anchor_query_result(
        'financial_anchors',
        'end_of_day_balances_2024_10_31',
        'SELECT
            account_id,
            account_type,
            SUM(CASE WHEN txn_type = ''DEBIT'' THEN amount ELSE 0 END) as total_debits,
            SUM(CASE WHEN txn_type = ''CREDIT'' THEN amount ELSE 0 END) as total_credits,
            SUM(CASE WHEN txn_type = ''CREDIT'' THEN amount ELSE -amount END) as balance
        FROM ledger
        WHERE txn_date = ''2024-10-31''
        GROUP BY account_id, account_type
        ORDER BY account_id',
        'End-of-day balances for reconciliation'
    );
    ```

=== "CTEs and Subqueries"

    ```sql
    SELECT anchor_query_result(
        'ml_anchors',
        'fraud_detection_training_v3.2',
        'WITH feature_engineering AS (
            SELECT
                transaction_id,
                amount,
                merchant_category,
                -- Feature: transaction amount z-score
                (amount - AVG(amount) OVER ()) / STDDEV(amount) OVER () as amount_zscore,
                -- Feature: user avg transaction amount
                AVG(amount) OVER (PARTITION BY user_id) as user_avg_amount,
                -- Label
                is_fraud
            FROM transactions
            WHERE dataset_version = ''v3.2''
        )
        SELECT * FROM feature_engineering
        ORDER BY transaction_id',
        'ML fraud detection training dataset v3.2 - 150K samples'
    );
    ```

=== "Window Functions"

    ```sql
    SELECT anchor_query_result(
        'analytics_anchors',
        'user_retention_cohort_2024_q3',
        'SELECT
            user_id,
            signup_date,
            cohort_month,
            months_since_signup,
            is_active,
            COUNT(*) OVER (PARTITION BY cohort_month) as cohort_size,
            SUM(CASE WHEN is_active THEN 1 ELSE 0 END) OVER (
                PARTITION BY cohort_month
                ORDER BY months_since_signup
            ) as active_users
        FROM user_activity_summary
        WHERE EXTRACT(QUARTER FROM signup_date) = 3
          AND EXTRACT(YEAR FROM signup_date) = 2024
        ORDER BY cohort_month, user_id',
        'Q3 2024 user retention cohort analysis'
    );
    ```

---

## Use Case Examples

### Use Case 1: Regulatory Compliance Reports

Anchor quarterly financial reports for SEC/SOX compliance.

```sql
-- End of Q2: Anchor report
SELECT anchor_query_result(
    'regulatory_anchors',
    'sox_report_2024_q2',
    'SELECT
        account_id,
        account_name,
        SUM(debit_amount) as total_debits,
        SUM(credit_amount) as total_credits,
        SUM(credit_amount) - SUM(debit_amount) as ending_balance
    FROM general_ledger
    WHERE fiscal_quarter = ''2024Q2''
    GROUP BY account_id, account_name
    HAVING ABS(SUM(credit_amount) - SUM(debit_amount)) > 0.01
    ORDER BY account_id',
    'SOX compliance report Q2 2024 - submitted to SEC on 2024-07-15'
);

-- Later (during audit): Verify report integrity
SELECT verify_query_anchor('regulatory_anchors', 'sox_report_2024_q2');
-- Returns TRUE = report data unchanged since submission
-- Returns FALSE = DATA TAMPERING - audit fails!
```

### Use Case 2: Machine Learning Dataset Provenance

Ensure ML training datasets remain immutable and reproducible.

```sql
-- Before training: Anchor training dataset
SELECT anchor_query_result(
    'ml_dataset_anchors',
    'churn_prediction_train_v2.5',
    'SELECT
        customer_id,
        tenure_months,
        monthly_charges,
        total_charges,
        contract_type,
        payment_method,
        internet_service,
        -- Features
        ARRAY[
            tenure_months::FLOAT,
            monthly_charges::FLOAT,
            total_charges::FLOAT,
            CASE WHEN contract_type = ''Month-to-month'' THEN 1 ELSE 0 END::FLOAT,
            CASE WHEN payment_method = ''Electronic check'' THEN 1 ELSE 0 END::FLOAT
        ] as features,
        -- Label
        churned::INT as label
    FROM customers
    WHERE dataset_split = ''train''
      AND dataset_version = ''2.5''
    ORDER BY customer_id',
    'Churn prediction training set v2.5 - 50K customers, 5 features'
);

-- In production: Verify model was trained on unaltered data
SELECT verify_query_anchor('ml_dataset_anchors', 'churn_prediction_train_v2.5');
```

**Benefits:**
- Ensure model reproducibility
- Detect data drift or corruption
- Comply with AI ethics regulations
- Maintain dataset lineage

### Use Case 3: Financial Reconciliation

Daily account balance snapshots with cryptographic proof.

```sql
-- End of each business day
DO $$
DECLARE
    business_date DATE := CURRENT_DATE;
BEGIN
    PERFORM anchor_query_result(
        'reconciliation_anchors',
        'daily_balances_' || business_date::TEXT,
        format('SELECT
            account_id,
            account_type,
            opening_balance,
            total_debits,
            total_credits,
            closing_balance,
            last_transaction_id
        FROM daily_balance_summary
        WHERE balance_date = %L
        ORDER BY account_id', business_date),
        'Daily account balances for ' || business_date::TEXT
    );

    RAISE NOTICE 'Anchored daily balances for %', business_date;
END $$;

-- Verify specific date
SELECT verify_query_anchor('reconciliation_anchors', 'daily_balances_2024-10-31');
```

### Use Case 4: Clinical Trial Data Lock

Lock database after clinical trial completion (FDA 21 CFR Part 11).

```sql
-- Database lock at trial completion
SELECT anchor_query_result(
    'clinical_trial_anchors',
    'trial_NCT12345678_final_lock',
    'SELECT
        patient_id,
        randomization_arm,
        primary_endpoint_value,
        primary_endpoint_date,
        secondary_endpoints,
        adverse_events,
        completion_status
    FROM trial_data
    WHERE trial_id = ''NCT12345678''
      AND data_quality_check = ''PASSED''
    ORDER BY patient_id',
    'Database locked 2024-10-31 - submitted to FDA 2024-11-05 - Principal Investigator: Dr. Smith'
);

-- FDA inspection: Verify data hasn't changed since lock
SELECT verify_query_anchor('clinical_trial_anchors', 'trial_NCT12345678_final_lock');
```

---

## Verification Workflows

### Scheduled Verification

Create a scheduled job to verify critical anchors:

```sql
-- Daily verification of all compliance anchors
DO $$
DECLARE
    anchor_rec RECORD;
    failed_count INTEGER := 0;
BEGIN
    FOR anchor_rec IN
        SELECT query_id FROM compliance_anchors
        WHERE anchor_time >= CURRENT_DATE - INTERVAL '30 days'
    LOOP
        IF NOT verify_query_anchor('compliance_anchors', anchor_rec.query_id) THEN
            RAISE WARNING 'VERIFICATION FAILED: %', anchor_rec.query_id;
            failed_count := failed_count + 1;
        END IF;
    END LOOP;

    IF failed_count > 0 THEN
        RAISE EXCEPTION 'CRITICAL: % anchor verifications failed - investigate immediately', failed_count;
    ELSE
        RAISE NOTICE 'All anchor verifications passed';
    END IF;
END $$;
```

### Batch Verification Report

Generate a verification report for all anchors:

```sql
-- Verify all anchors and generate report
SELECT
    query_id,
    verify_query_anchor('query_anchors', query_id) as verified,
    anchor_time,
    notes,
    CASE
        WHEN verify_query_anchor('query_anchors', query_id) THEN 'PASS'
        ELSE 'FAIL - INVESTIGATE'
    END as status
FROM query_anchors
WHERE anchor_time >= '2024-01-01'
ORDER BY anchor_time DESC;
```

**Output:**
```
      query_id           | verified |       anchor_time        |           notes            |      status
-------------------------+----------+--------------------------+----------------------------+------------------
 monthly_sales_2024_10    | t        | 2025-10-31 14:00:00+00   | Monthly sales summary      | PASS
 sox_report_2024_q2       | t        | 2024-07-15 16:00:00+00   | SOX compliance Q2          | PASS
 customer_data_export     | f        | 2024-06-01 10:00:00+00   | GDPR export request        | FAIL - INVESTIGATE
```

---

## Best Practices

### 1. Choose Meaningful Query IDs

!!! tip "Good Query ID Naming"
    ```sql
    -- Good: Descriptive, includes date/version
    'financial_report_2024_q3_v2'
    'ml_training_fraud_detection_v1.5'
    'customer_snapshot_2024_10_31'

    -- Bad: Generic, no context
    'query1'
    'test'
    'report'
    ```

### 2. Include ORDER BY for Determinism

!!! warning "Critical for Reproducibility"
    ```sql
    -- ALWAYS include ORDER BY in anchored queries
    SELECT anchor_query_result(
        'anchors',
        'my_query',
        'SELECT * FROM table ORDER BY id',  -- ✓ Deterministic
        'notes'
    );

    -- DON'T: Omit ORDER BY (non-deterministic results)
    SELECT anchor_query_result(
        'anchors',
        'my_query',
        'SELECT * FROM table',  -- ✗ May return different order
        'notes'
    );
    ```

### 3. Add Comprehensive Notes

```sql
SELECT anchor_query_result(
    'compliance_anchors',
    'audit_report_2024_q3',
    'SELECT ... ORDER BY ...',
    'Q3 2024 internal audit report - Created by: Jane Smith - Purpose: SOX compliance - Approved by: John Doe - Submission deadline: 2024-10-15'
);
```

### 4. Re-anchor for Legitimate Updates

```sql
-- When data legitimately changes, create new anchor
SELECT anchor_query_result(
    'dataset_anchors',
    'ml_training_v2.6',  -- New version number
    'SELECT ... ORDER BY ...',
    'Updated training set with corrected labels - Previous version: v2.5'
);
```

### 5. Archive Old Anchors

```sql
-- Move old anchors to archive table
CREATE BLOCKCHAIN TABLE query_anchors_archive AS
SELECT * FROM query_anchors
WHERE anchor_time < CURRENT_DATE - INTERVAL '1 year';

-- Keep only recent anchors in main table (if needed for performance)
-- Note: Deleting from blockchain tables is not allowed
-- Instead, use views to filter
CREATE VIEW query_anchors_recent AS
SELECT * FROM query_anchors
WHERE anchor_time >= CURRENT_DATE - INTERVAL '90 days';
```

---

## Performance Considerations

### Query Complexity Impact

| Query Type | Rows | Columns | Hash Time | Notes |
|------------|------|---------|-----------|-------|
| Simple SELECT | 100 | 5 | ~5 ms | Fast |
| Simple SELECT | 10K | 5 | ~500 ms | Moderate |
| Complex JOIN | 10K | 20 | ~2 sec | Sorting overhead |
| Aggregation | 1M → 100 | 10 | ~1 sec | Most time in query execution |

!!! tip "Optimization Tips"
    - **Index source tables** - Query execution time dominates hash computation
    - **Limit result size** - Anchor aggregated/filtered results, not raw tables
    - **Anchor during off-peak** - Schedule large query anchors for low-traffic periods
    - **Use materialized views** - Pre-compute complex queries, then anchor the view

### Deterministic Sorting Overhead

The anchoring process sorts results deterministically:

```
Time Breakdown:
┌────────────────────────────┐
│ Query Execution:    60%    │  ← Optimize with indexes
├────────────────────────────┤
│ Deterministic Sort: 25%    │  ← Unavoidable overhead
├────────────────────────────┤
│ SHA-256 Hashing:    10%    │  ← Fixed cost
├────────────────────────────┤
│ Storage:            5%     │  ← Negligible
└────────────────────────────┘
```

---

## Common Pitfalls

### Pitfall 1: Non-Deterministic Queries

!!! danger "Avoid NOW(), RANDOM(), etc."
    ```sql
    -- BAD: Non-deterministic functions
    SELECT anchor_query_result(
        'anchors',
        'bad_query',
        'SELECT *, NOW() FROM table',  -- ✗ NOW() changes every execution
        'notes'
    );

    -- GOOD: Use fixed timestamp or exclude
    SELECT anchor_query_result(
        'anchors',
        'good_query',
        'SELECT * FROM table WHERE created_at <= ''2024-10-31''',  -- ✓ Fixed date
        'notes'
    );
    ```

### Pitfall 2: Missing ORDER BY

!!! warning "PostgreSQL doesn't guarantee order without ORDER BY"
    ```sql
    -- Verification may fail randomly if data order changes
    SELECT anchor_query_result('anchors', 'unstable',
        'SELECT * FROM table', 'notes');  -- ✗ No ORDER BY

    -- Always specify ORDER BY
    SELECT anchor_query_result('anchors', 'stable',
        'SELECT * FROM table ORDER BY id', 'notes');  -- ✓ Deterministic
    ```

### Pitfall 3: Anchoring Transient Data

!!! warning "Don't anchor temporary tables or data that will be deleted"
    ```sql
    -- BAD: Anchoring temp table
    CREATE TEMP TABLE temp_results AS SELECT ...;
    SELECT anchor_query_result('anchors', 'temp',
        'SELECT * FROM temp_results ORDER BY id', 'notes');  -- ✗ Table will be dropped

    -- GOOD: Anchor permanent data
    SELECT anchor_query_result('anchors', 'permanent',
        'SELECT * FROM permanent_table ORDER BY id', 'notes');  -- ✓ Data persists
    ```

---

## Next Steps

- **[Verification](verification.md)** - Learn advanced verification techniques
- **[SQL Functions Reference](../api/sql-functions-reference.md)** - Detailed API documentation
- **[Examples](../examples/index.md)** - Real-world query anchoring scenarios

---

## See Also

- [Architecture: Query Anchoring Implementation](../architecture/blockchain-access-method.md)
- [QUERY_ANCHORING_DOCUMENTATION.md](/home/eagle/Code/BC_Postgres/postgres_test/QUERY_ANCHORING_DOCUMENTATION.md) - Technical deep dive
- [Patent Documentation](../architecture/index.md) - Intellectual property details
