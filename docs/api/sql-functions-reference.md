# SQL Functions Reference - PostgreSQL Blockchain Extension

## Overview

This document provides comprehensive reference for all SQL functions provided by the PostgreSQL Blockchain Extension. These functions enable users to interact with blockchain tables, access system columns, and perform blockchain-specific operations.

## Function Categories

- [Query Result Anchoring Functions](#query-result-anchoring-functions) ⭐ **Core Feature**
- [Table Management Functions](#table-management-functions)
- [System Column Access Functions](#system-column-access-functions)
- [Hash Chain Functions](#hash-chain-functions)
- [Counter Management Functions](#counter-management-functions)
- [Audit and Monitoring Functions](#audit-and-monitoring-functions)

---

## Query Result Anchoring Functions

**Patent**: "Tamper-Evident Anchoring of Arbitrary SQL Query Results Using Blockchain Tables in Relational Databases"

These functions implement tamper-evident anchoring of arbitrary SQL query results, enabling cryptographic proof that query results have not been modified.

### create_anchor_table(table_name text)

**Description**: Creates a blockchain table specifically designed for storing query anchors with metadata.

**Category**: DDL / Table Creation

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `table_name` | TEXT | Yes | Name of the blockchain anchor table to create |

**Returns**: `TEXT` - Confirmation message

**Errors**:
- `ERROR: table name cannot be NULL` - table_name is NULL
- `ERROR: invalid table name` - table_name contains invalid characters (quotes)
- `ERROR: relation "table_name" already exists` - Table already exists

**Created Table Schema**:
```sql
CREATE BLOCKCHAIN TABLE <table_name> (
  query_id      TEXT NOT NULL,           -- Unique identifier for the query
  query_text    TEXT NOT NULL,           -- The SQL query that was executed
  result_hash   BYTEA NOT NULL,          -- SHA-256 hash of query result
  anchor_time   TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,  -- When anchored
  user_id       TEXT NOT NULL DEFAULT CURRENT_USER,              -- Who anchored
  notes         TEXT                     -- Optional notes/metadata
  -- Plus 9 automatic system columns (__row_id, __curr_hash, etc.)
);
```

**Example**:
```sql
-- Create anchor table for storing query results
SELECT create_anchor_table('financial_report_anchors');
-- Returns: 'Anchor table created successfully'

-- Verify table was created
\d financial_report_anchors
```

**Table Structure**:
```
                Table "public.financial_report_anchors"
   Column    |           Type           | Nullable |        Default
-------------+--------------------------+----------+------------------------
 query_id    | text                     | not null |
 query_text  | text                     | not null |
 result_hash | bytea                    | not null |
 anchor_time | timestamp with time zone | not null | CURRENT_TIMESTAMP
 user_id     | text                     | not null | CURRENT_USER
 notes       | text                     |          |

System columns (hidden):
 __row_id       | uuid
 __curr_hash    | bytea
 __prev_hash    | bytea
 __tx_type      | text
 __tx_lsn       | bigint
 __tx_origin    | uuid
 __tx_version   | integer
 __is_latest    | boolean
 __tx_timestamp | timestamp with time zone
```

**Notes**:
- Creates a blockchain table (immutable, tamper-evident)
- Automatically includes 9 system columns for blockchain functionality
- Table cannot be modified or deleted (DDL protection)
- Ideal for compliance, audit, and regulatory reporting

**Related Functions**:
- `anchor_query_result()` - Store query results in this table
- `verify_query_anchor()` - Verify query results

---

### anchor_query_result(anchor_table text, query_id text, query_text text, notes text DEFAULT NULL)

**Description**: Executes an arbitrary SQL query, deterministically serializes and sorts the result set, computes a SHA-256 hash, and stores it in a blockchain anchor table along with metadata.

**Category**: Query Anchoring / Cryptographic Hashing

**Parameters**:
| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `anchor_table` | TEXT | Yes | - | Name of blockchain table to store anchors |
| `query_id` | TEXT | Yes | - | Unique identifier for this query/anchor |
| `query_text` | TEXT | Yes | - | The SQL query to execute and anchor |
| `notes` | TEXT | No | NULL | Optional notes about this anchor |

**Returns**: `BYTEA` - SHA-256 hash of the query result (32 bytes)

**Algorithm**:
1. Execute the SQL query using SPI (Server Programming Interface)
2. Sort result tuples lexicographically by all columns (deterministic ordering)
3. Serialize each tuple to text format
4. Compute SHA-256 hash of serialized result set
5. Store hash, query text, and metadata in blockchain anchor table

**Deterministic Serialization**:
```
Hash = SHA256(
  query_text ||
  sorted_row_1_serialized ||
  sorted_row_2_serialized ||
  ...
  sorted_row_N_serialized
)
```

**Errors**:
- `ERROR: anchor_table cannot be NULL` - anchor_table parameter is NULL
- `ERROR: query_id cannot be NULL` - query_id parameter is NULL
- `ERROR: query_text cannot be NULL` - query_text parameter is NULL
- `ERROR: invalid anchor table name` - anchor_table contains invalid characters
- `ERROR: Query execution failed: <code>` - SQL query failed to execute
- `ERROR: Query returned no tuple table` - Query execution error
- `ERROR: Failed to insert anchor: <code>` - Failed to insert into anchor table

**Example**:
```sql
-- Create anchor table first
SELECT create_anchor_table('financial_report_anchors');

-- Anchor a simple query
SELECT anchor_query_result(
  'financial_report_anchors',
  'q1_revenue_2025',
  'SELECT department, SUM(revenue) FROM sales WHERE year = 2025 AND quarter = 1 GROUP BY department ORDER BY department',
  'Q1 2025 revenue by department for annual report'
);

-- Returns: \x1a2b3c4d5e6f789a0b1c2d3e4f5678901a2b3c4d5e6f789a0b1c2d3e4f567890
-- Also prints: INFO:  Anchoring query: q1_revenue_2025
--              INFO:  Query anchor stored successfully with hash: <hex>
```

**Complex Query Examples**:
```sql
-- Anchor JOIN query
SELECT anchor_query_result(
  'financial_report_anchors',
  'customer_summary_fy25',
  $$
    SELECT c.customer_id, c.name, COUNT(o.order_id) AS order_count, SUM(o.amount) AS total_amount
    FROM customers c
    JOIN orders o ON c.customer_id = o.customer_id
    WHERE o.order_date BETWEEN '2025-01-01' AND '2025-12-31'
    GROUP BY c.customer_id, c.name
    ORDER BY c.customer_id
  $$,
  'FY2025 customer transaction summary for audit'
);

-- Anchor aggregation query
SELECT anchor_query_result(
  'financial_report_anchors',
  'monthly_sales_2025',
  $$
    SELECT date_trunc('month', sale_date) AS month,
           SUM(amount) AS total_sales,
           COUNT(*) AS transaction_count
    FROM sales
    WHERE sale_date BETWEEN '2025-01-01' AND '2025-12-31'
    GROUP BY month
    ORDER BY month
  $$,
  'Monthly sales totals for 2025'
);
```

**Verification Workflow**:
```sql
-- 1. Anchor query result
SELECT anchor_query_result(
  'financial_report_anchors',
  'q1_revenue_2025',
  'SELECT department, SUM(revenue) FROM sales WHERE year = 2025 AND quarter = 1 GROUP BY department ORDER BY department',
  'Q1 2025 revenue'
);

-- 2. View stored anchor
SELECT query_id, anchor_time, user_id, encode(result_hash, 'hex') AS hash
FROM financial_report_anchors
WHERE query_id = 'q1_revenue_2025';

-- 3. Later, verify integrity (see verify_query_anchor)
SELECT verify_query_anchor('financial_report_anchors', 'q1_revenue_2025');
```

**Notes**:
- **Deterministic**: Same query + same data = same hash (reproducible)
- **Query Complexity**: Supports JOINs, aggregations, subqueries, CTEs, window functions
- **Performance**: Hash computation time scales with result set size
- **Transaction Safety**: Executes in read-only mode, does not modify source data
- **Uniqueness**: query_id should be unique per anchor (no enforced constraint)
- **Use Cases**: Compliance reporting, audit trails, regulatory snapshots, data integrity proofs

**Performance Considerations**:
```sql
-- Large result sets (millions of rows) may take seconds to hash
-- Consider limiting result sets or using summary queries

-- Example: Hash summary instead of raw data
SELECT anchor_query_result(
  'financial_report_anchors',
  'daily_summary_2025',
  'SELECT date, SUM(amount), COUNT(*) FROM sales GROUP BY date ORDER BY date',
  'Daily summaries instead of raw transactions'
);
```

---

### verify_query_anchor(anchor_table text, query_id text)

**Description**: Verifies the integrity of a previously anchored query by re-executing the query, recomputing the hash, and comparing it with the stored hash. Returns TRUE if verification passes (hashes match), FALSE if tampering is detected.

**Category**: Verification / Integrity Checking

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `anchor_table` | TEXT | Yes | Name of blockchain anchor table |
| `query_id` | TEXT | Yes | The query identifier to verify |

**Returns**: `BOOLEAN`
- `TRUE` - Verification passed (hashes match, data unchanged)
- `FALSE` - Verification failed (hashes differ, data tampered or changed)

**Verification Algorithm**:
1. Retrieve stored `query_text` and `result_hash` from anchor table
2. Re-execute the original query
3. Recompute hash using same deterministic algorithm
4. Compare stored hash with computed hash
5. Return TRUE if match, FALSE if mismatch

**Errors**:
- `ERROR: anchor_table cannot be NULL` - anchor_table parameter is NULL
- `ERROR: query_id cannot be NULL` - query_id parameter is NULL
- `ERROR: invalid anchor table name` - anchor_table contains invalid characters
- `ERROR: No anchor found for query_id: <id>` - query_id not found in anchor table
- `ERROR: Query execution failed during verification` - Query re-execution failed

**Example**:
```sql
-- Verify a previously anchored query
SELECT verify_query_anchor('financial_report_anchors', 'q1_revenue_2025');
-- Returns: true (if data unchanged)
-- Returns: false (if data modified)

-- Also prints:
-- INFO:  Verifying query anchor: q1_revenue_2025
-- INFO:  Stored hash: <hex>
-- INFO:  Computed hash: <hex>
-- INFO:  Verification result: PASSED (or FAILED)
```

**Verification Scenarios**:

**Scenario 1: Data Unchanged (Verification Passes)**
```sql
-- Original anchoring
SELECT anchor_query_result(
  'financial_report_anchors',
  'q1_revenue_2025',
  'SELECT department, SUM(revenue) FROM sales WHERE year = 2025 AND quarter = 1 GROUP BY department ORDER BY department',
  'Q1 2025 revenue'
);

-- Later verification (no changes to sales data)
SELECT verify_query_anchor('financial_report_anchors', 'q1_revenue_2025');
-- Returns: true ✓
-- INFO: Verification result: PASSED
```

**Scenario 2: Data Modified (Verification Fails)**
```sql
-- Original anchoring
SELECT anchor_query_result(
  'financial_report_anchors',
  'q1_revenue_2025',
  'SELECT department, SUM(revenue) FROM sales WHERE year = 2025 AND quarter = 1 GROUP BY department ORDER BY department',
  'Q1 2025 revenue'
);

-- Someone modifies the source data (tampering!)
UPDATE sales SET revenue = revenue * 1.1 WHERE year = 2025 AND quarter = 1;

-- Verification detects tampering
SELECT verify_query_anchor('financial_report_anchors', 'q1_revenue_2025');
-- Returns: false ✗
-- INFO: Verification result: FAILED
-- INFO: Hash mismatch - data may have been tampered with
```

**Comprehensive Verification Example**:
```sql
-- Verify all anchored queries
SELECT
  query_id,
  anchor_time,
  user_id,
  verify_query_anchor('financial_report_anchors', query_id) AS verified,
  CASE
    WHEN verify_query_anchor('financial_report_anchors', query_id) THEN 'PASS'
    ELSE 'FAIL - TAMPERING DETECTED'
  END AS status
FROM financial_report_anchors
ORDER BY anchor_time DESC;
```

**Sample Output**:
```
     query_id      |        anchor_time         |   user_id   | verified |        status
-------------------+----------------------------+-------------+----------+----------------------
 q1_revenue_2025   | 2025-08-12 10:30:00+00     | analyst_1   | t        | PASS
 monthly_sales_2025| 2025-08-12 09:15:00+00     | analyst_2   | t        | PASS
 customer_summary  | 2025-08-11 14:20:00+00     | manager_1   | f        | FAIL - TAMPERING DETECTED
```

**Automated Verification Script**:
```sql
-- Create verification report
DO $$
DECLARE
  r RECORD;
  failed_count INTEGER := 0;
BEGIN
  FOR r IN SELECT query_id FROM financial_report_anchors LOOP
    IF NOT verify_query_anchor('financial_report_anchors', r.query_id) THEN
      RAISE WARNING 'VERIFICATION FAILED for query_id: %', r.query_id;
      failed_count := failed_count + 1;
    END IF;
  END LOOP;

  IF failed_count > 0 THEN
    RAISE EXCEPTION 'Verification failed for % queries - data tampering detected!', failed_count;
  ELSE
    RAISE NOTICE 'All queries verified successfully - data integrity confirmed';
  END IF;
END $$;
```

**Notes**:
- **Performance**: Verification requires re-executing the query (can be slow for complex queries)
- **Data Changes**: Any change to source data will cause verification to fail
- **Deterministic**: Uses exact same hashing algorithm as `anchor_query_result()`
- **Use Cases**: Regular integrity checks, audit compliance, forensic investigation
- **Best Practice**: Run verification regularly (e.g., daily, weekly) to detect tampering
- **False Positives**: Legitimate data updates will cause verification to fail (expected behavior)

**Verification Best Practices**:
```sql
-- Schedule regular verification (using cron or pg_cron)
-- Example: Verify all anchors daily

-- 1. Create verification log table
CREATE TABLE anchor_verification_log (
  verification_time TIMESTAMPTZ DEFAULT CURRENT_TIMESTAMP,
  query_id TEXT,
  verification_result BOOLEAN,
  stored_hash BYTEA,
  computed_hash BYTEA
);

-- 2. Run verification and log results
INSERT INTO anchor_verification_log (query_id, verification_result)
SELECT query_id, verify_query_anchor('financial_report_anchors', query_id)
FROM financial_report_anchors;

-- 3. Alert on failures
SELECT * FROM anchor_verification_log
WHERE verification_result = false
  AND verification_time > NOW() - INTERVAL '1 day';
```

**Related Functions**:
- `create_anchor_table()` - Create anchor storage table
- `anchor_query_result()` - Anchor query results

---

## Table Management Functions

### is_blockchain_table(table_name text)

**Description**: Check if a table is a blockchain table

**Parameters**:
- `table_name` (text): Name of the table to check

**Returns**: boolean

**Example**:
```sql
SELECT is_blockchain_table('my_audit_log');
-- Returns: true if my_audit_log is a blockchain table

SELECT is_blockchain_table('regular_table');
-- Returns: false if regular_table is not a blockchain table
```

**Notes**:
- Function is bootstrap-safe (SQL-only, no PL/pgSQL dependency)
- Searches in the 'public' schema by default
- Returns false for non-existent tables

### list_blockchain_tables()

**Description**: List all blockchain tables in the database

**Parameters**: None

**Returns**: TABLE(schema_name name, table_name name)

**Example**:
```sql
SELECT * FROM list_blockchain_tables();
```

**Sample Output**:
```
 schema_name | table_name
-------------+-------------
 public      | audit_log
 public      | transactions
 finance     | payments
```

**Notes**:
- Returns tables from all schemas
- Results are ordered by schema name, then table name
- Bootstrap-safe function

### describe_blockchain_table(table_name text)

**Description**: Describe the structure of a blockchain table including system columns

**Parameters**:
- `table_name` (text): Name of the blockchain table

**Returns**: TABLE(column_name text, data_type text, is_system_column boolean, description text)

**Example**:
```sql
SELECT * FROM describe_blockchain_table('audit_log');
```

**Sample Output**:
```
  column_name   |    data_type     | is_system_column |        description
----------------+------------------+------------------+---------------------------
 user_id        | integer          | f                | User-defined column
 action         | character varying| f                | User-defined column
 __row_id       | uuid             | t                | Unique row identifier
 __curr_hash    | bytea            | t                | Current row hash
 __prev_hash    | bytea            | t                | Previous row hash
 __tx_lsn       | bigint           | t                | Global counter value
 __tx_timestamp | timestamptz      | t                | Transaction timestamp
```

**Notes**:
- Requires PL/pgSQL (conditionally loaded)
- Shows both user and system columns
- Provides detailed descriptions of each column

## System Column Access Functions

### get_blockchain_system_columns(table_name text, row_identifier uuid)

**Description**: Get system column values for a specific row

**Parameters**:
- `table_name` (text): Name of the blockchain table
- `row_identifier` (uuid): The __row_id value of the target row

**Returns**: TABLE(column_name text, column_value text)

**Example**:
```sql
SELECT * FROM get_blockchain_system_columns('audit_log', 
    '550e8400-e29b-41d4-a716-446655440000');
```

**Sample Output**:
```
  column_name   |               column_value
----------------+----------------------------------------
 __row_id       | 550e8400-e29b-41d4-a716-446655440000
 __curr_hash    | \x1a2b3c4d5e6f...
 __prev_hash    | \x9f8e7d6c5b4a...
 __tx_type      | INSERT
 __tx_lsn       | 1001
 __tx_origin    | 123e4567-e89b-12d3-a456-426614174000
 __tx_version   | 1
 __is_latest    | t
 __tx_timestamp | 2025-08-12 10:30:00+00
```

### get_blockchain_audit_data(table_name text, limit_rows integer DEFAULT 100)

**Description**: Get audit trail data from a blockchain table including system columns

**Parameters**:
- `table_name` (text): Name of the blockchain table
- `limit_rows` (integer, optional): Maximum number of rows to return (default: 100)

**Returns**: TABLE with all user columns plus selected system columns

**Example**:
```sql
SELECT * FROM get_blockchain_audit_data('audit_log', 50);
```

**Sample Output**:
```
 user_id |  action  |      ip_address      | __tx_lsn | __curr_hash | __tx_timestamp
---------+----------+----------------------+----------+-------------+------------------------
    1001 | login    | 192.168.1.100        |     1001 | \x1a2b3c... | 2025-08-12 10:30:00+00
    1002 | logout   | 192.168.1.100        |     1002 | \x2b3c4d... | 2025-08-12 10:35:00+00
```

**Notes**:
- Combines user data with key system columns
- Ordered by __tx_lsn (insertion order)
- Useful for audit trail reporting

## Hash Chain Functions

### verify_hash_chain(table_name text, start_counter bigint DEFAULT 1, end_counter bigint DEFAULT NULL)

**Description**: Verify the integrity of the hash chain in a blockchain table

**Parameters**:
- `table_name` (text): Name of the blockchain table
- `start_counter` (bigint, optional): Starting counter value (default: 1)
- `end_counter` (bigint, optional): Ending counter value (default: all rows)

**Returns**: TABLE(counter_value bigint, hash_valid boolean, error_message text)

**Example**:
```sql
-- Verify entire chain
SELECT * FROM verify_hash_chain('audit_log');

-- Verify specific range
SELECT * FROM verify_hash_chain('audit_log', 100, 200);
```

**Sample Output**:
```
 counter_value | hash_valid |    error_message
---------------+------------+-------------------
          1001 | t          | 
          1002 | t          | 
          1003 | f          | Hash mismatch detected
          1004 | t          | 
```

**Notes**:
- Returns one row per verified counter value
- `hash_valid` is false if tampering is detected
- `error_message` provides details when hash_valid is false

### get_hash_chain_info(table_name text, counter_value bigint)

**Description**: Get detailed hash chain information for a specific row

**Parameters**:
- `table_name` (text): Name of the blockchain table  
- `counter_value` (bigint): Counter value of the target row

**Returns**: TABLE(counter bigint, current_hash text, previous_hash text, timestamp timestamptz, chain_position text)

**Example**:
```sql
SELECT * FROM get_hash_chain_info('audit_log', 1001);
```

**Sample Output**:
```
 counter | current_hash                     | previous_hash                    |      timestamp      | chain_position
---------+----------------------------------+----------------------------------+--------------------+---------------
    1001 | 1a2b3c4d5e6f789a0b1c2d3e4f56... | 0000000000000000000000000000... | 2025-08-12 10:30:00| first_row
```

**Notes**:
- `chain_position` indicates if this is 'first_row', 'middle_row', or 'last_row'
- Hash values are returned as hex strings for readability
- Returns NULL if counter value doesn't exist

### compute_row_hash(table_name text, counter_value bigint)

**Description**: Recompute the hash for a specific row and compare with stored hash

**Parameters**:
- `table_name` (text): Name of the blockchain table
- `counter_value` (bigint): Counter value of the target row

**Returns**: TABLE(stored_hash text, computed_hash text, hash_matches boolean)

**Example**:
```sql
SELECT * FROM compute_row_hash('audit_log', 1001);
```

**Sample Output**:
```
           stored_hash            |          computed_hash          | hash_matches
----------------------------------+---------------------------------+--------------
 1a2b3c4d5e6f789a0b1c2d3e4f56... | 1a2b3c4d5e6f789a0b1c2d3e4f56...| t
```

**Notes**:
- Useful for detecting tampering in specific rows
- `hash_matches` will be false if data has been tampered with
- Computationally expensive for large rows

## Counter Management Functions

### get_blockchain_counters()

**Description**: Get current counter values for all blockchain tables

**Parameters**: None

**Returns**: TABLE(table_oid oid, table_name text, schema_name text, current_counter bigint, last_persisted bigint)

**Example**:
```sql
SELECT * FROM get_blockchain_counters();
```

**Sample Output**:
```
 table_oid | table_name | schema_name | current_counter | last_persisted
-----------+------------+-------------+-----------------+----------------
     16384 | audit_log  | public      |            1050 |           1000
     16385 | payments   | finance     |             325 |            300
```

**Notes**:
- Shows active counters for all blockchain tables
- `last_persisted` shows last value written to disk
- Useful for monitoring counter system status

### get_next_counter_preview(table_name text)

**Description**: Preview what the next counter value would be without consuming it

**Parameters**:
- `table_name` (text): Name of the blockchain table

**Returns**: bigint

**Example**:
```sql
SELECT get_next_counter_preview('audit_log');
-- Returns: 1051 (if current counter is 1050)
```

**Notes**:
- Does not increment the actual counter
- Useful for applications that need to know the next value
- May trigger lazy recovery if counter not initialized

### reset_blockchain_counter(table_name text, new_value bigint DEFAULT 0)

**Description**: Reset counter for a blockchain table (DANGEROUS - use with caution)

**Parameters**:
- `table_name` (text): Name of the blockchain table
- `new_value` (bigint, optional): New counter value (default: 0)

**Returns**: boolean (true if successful)

**Example**:
```sql
-- Reset counter to 0 (only works on empty tables)
SELECT reset_blockchain_counter('new_table', 0);

-- Set counter to specific value (advanced use)
SELECT reset_blockchain_counter('imported_table', 5000);
```

**Notes**:
- **DANGEROUS**: Can break hash chain integrity
- Only safe to use on empty tables or during data migration
- Requires superuser privileges
- Should only be used by database administrators

## Audit and Monitoring Functions

### get_blockchain_statistics(table_name text DEFAULT NULL)

**Description**: Get detailed statistics for blockchain tables

**Parameters**:
- `table_name` (text, optional): Specific table name (default: all tables)

**Returns**: TABLE(table_name text, total_rows bigint, hash_chain_length bigint, avg_row_size bigint, total_size text, last_insert_time timestamptz)

**Example**:
```sql
-- All blockchain tables
SELECT * FROM get_blockchain_statistics();

-- Specific table
SELECT * FROM get_blockchain_statistics('audit_log');
```

**Sample Output**:
```
 table_name | total_rows | hash_chain_length | avg_row_size | total_size | last_insert_time
------------+------------+-------------------+--------------+------------+---------------------
 audit_log  |       1050 |              1050 |          256 | 269 MB     | 2025-08-12 10:45:00
 payments   |        325 |               325 |          512 | 166 MB     | 2025-08-12 09:30:00
```

### get_blockchain_performance_metrics(time_window interval DEFAULT '1 hour')

**Description**: Get performance metrics for blockchain operations

**Parameters**:
- `time_window` (interval, optional): Time window for metrics (default: 1 hour)

**Returns**: TABLE(metric_name text, metric_value numeric, unit text, measurement_time timestamptz)

**Example**:
```sql
-- Last hour metrics
SELECT * FROM get_blockchain_performance_metrics();

-- Last 24 hours
SELECT * FROM get_blockchain_performance_metrics('24 hours');
```

**Sample Output**:
```
      metric_name       | metric_value |    unit    |   measurement_time
------------------------+--------------+------------+---------------------
 inserts_per_second     |       25.5   | ops/sec    | 2025-08-12 10:45:00
 avg_insert_time        |        4.2   | ms         | 2025-08-12 10:45:00
 avg_hash_time          |        1.8   | ms         | 2025-08-12 10:45:00
 counter_operations     |      1530    | count      | 2025-08-12 10:45:00
```

### get_blockchain_security_events(time_window interval DEFAULT '1 day')

**Description**: Get security-related events for blockchain tables

**Parameters**:
- `time_window` (interval, optional): Time window for events (default: 1 day)

**Returns**: TABLE(event_time timestamptz, event_type text, table_name text, user_name text, client_addr inet, details text)

**Example**:
```sql
SELECT * FROM get_blockchain_security_events();
```

**Sample Output**:
```
     event_time      |   event_type   | table_name | user_name |  client_addr  |        details
---------------------+----------------+------------+-----------+---------------+-------------------------
 2025-08-12 10:30:00 | blocked_update | audit_log  | app_user  | 192.168.1.100 | UPDATE attempt blocked
 2025-08-12 09:15:00 | blocked_delete | payments   | bad_actor | 10.0.0.50     | DELETE attempt blocked
```

**Notes**:
- Events are logged automatically by the protection system
- Useful for security monitoring and compliance
- Can help identify potential attacks or misuse

## Advanced Functions

### export_blockchain_data(table_name text, format text DEFAULT 'csv', include_system_columns boolean DEFAULT false)

**Description**: Export blockchain table data in various formats

**Parameters**:
- `table_name` (text): Name of the blockchain table
- `format` (text, optional): Export format ('csv', 'json', 'xml') (default: 'csv')
- `include_system_columns` (boolean, optional): Include system columns (default: false)

**Returns**: text (formatted export data)

**Example**:
```sql
-- Export as CSV (user columns only)
SELECT export_blockchain_data('audit_log', 'csv');

-- Export as JSON with system columns
SELECT export_blockchain_data('audit_log', 'json', true);
```

### import_blockchain_verification_data(table_name text, verification_data text)

**Description**: Import external verification data and validate against blockchain

**Parameters**:
- `table_name` (text): Name of the blockchain table
- `verification_data` (text): External verification data (JSON format)

**Returns**: TABLE(row_counter bigint, verification_status text, error_details text)

**Example**:
```sql
SELECT * FROM import_blockchain_verification_data('audit_log', 
    '{"counter": 1001, "expected_hash": "1a2b3c...", "timestamp": "2025-08-12T10:30:00Z"}');
```

## Utility Functions

### blockchain_table_health_check(table_name text)

**Description**: Perform comprehensive health check on a blockchain table

**Parameters**:
- `table_name` (text): Name of the blockchain table

**Returns**: TABLE(check_name text, status text, details text, recommendation text)

**Example**:
```sql
SELECT * FROM blockchain_table_health_check('audit_log');
```

**Sample Output**:
```
    check_name     | status |   details    |     recommendation
-------------------+--------+--------------+-----------------------
 hash_chain        | OK     | All valid    | None
 counter_sequence  | OK     | No gaps      | None  
 system_columns    | OK     | All present  | None
 table_size        | WARN   | 500MB large  | Consider archiving
```

### optimize_blockchain_table(table_name text)

**Description**: Perform optimization operations on a blockchain table

**Parameters**:
- `table_name` (text): Name of the blockchain table

**Returns**: TABLE(operation text, status text, details text)

**Example**:
```sql
SELECT * FROM optimize_blockchain_table('audit_log');
```

**Notes**:
- Performs safe optimization operations only
- Does not modify data or break immutability
- May update table statistics and clean up temporary files

## Error Handling

All blockchain functions follow consistent error handling patterns:

### Common Error Conditions

- **Table Not Found**: `ERROR: table "table_name" does not exist`
- **Not a Blockchain Table**: `ERROR: table "table_name" is not a blockchain table`
- **Invalid Counter**: `ERROR: counter value 123 does not exist in table`
- **Access Denied**: `ERROR: permission denied for blockchain operation`
- **System Error**: `ERROR: blockchain system error: [details]`

### Error Codes

Functions use standard PostgreSQL error codes:
- `42P01` - Table not found
- `42809` - Wrong object type (not a blockchain table)
- `22023` - Invalid parameter value
- `42501` - Insufficient privileges
- `XX000` - Internal error

## Performance Considerations

### Function Performance Tiers

**Fast Functions** (< 1ms):
- `is_blockchain_table()`
- `list_blockchain_tables()`
- `get_next_counter_preview()`

**Medium Functions** (1-10ms):
- `describe_blockchain_table()`
- `get_blockchain_system_columns()`
- `get_blockchain_counters()`

**Slow Functions** (10ms+):
- `verify_hash_chain()`
- `compute_row_hash()`
- `export_blockchain_data()`

### Best Practices

1. **Use LIMIT clauses** with audit functions on large tables
2. **Cache results** of describe_blockchain_table() calls
3. **Run verification functions** during off-peak hours
4. **Monitor performance** of export functions on large datasets

---

This SQL functions reference provides comprehensive documentation for all user-facing functions in the PostgreSQL Blockchain Extension, enabling effective interaction with blockchain tables and their unique features.