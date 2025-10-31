# Audit Logging Example

**Difficulty**: Beginner to Intermediate
**Time to Complete**: 15-20 minutes
**Prerequisites**: PostgreSQL with blockchain extension

## Overview

This example demonstrates a real-world audit logging system using blockchain tables. It covers user actions, system events, compliance reporting, and retention policies. This is one of the most common use cases for blockchain tables in enterprise environments.

## Use Case

You're building an audit system for a healthcare application that must comply with HIPAA regulations. Every access to patient records, data modifications, and administrative actions must be logged immutably with tamper-evident guarantees.

## Setup

### Step 1: Create Audit Log Table

```sql
-- Create blockchain table for audit logs
CREATE BLOCKCHAIN TABLE audit_log (
    event_id SERIAL,
    event_type VARCHAR(50) NOT NULL,
    event_category VARCHAR(30) NOT NULL,
    user_id INTEGER NOT NULL,
    username VARCHAR(100) NOT NULL,
    resource_type VARCHAR(50),
    resource_id VARCHAR(100),
    action VARCHAR(50) NOT NULL,
    details JSONB,
    ip_address INET,
    user_agent TEXT,
    success BOOLEAN DEFAULT true,
    error_message TEXT,
    timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

**Expected Output**:
```
CREATE TABLE
```

**Explanation**:
- `event_type`: LOGIN, LOGOUT, DATA_ACCESS, DATA_MODIFY, ADMIN_ACTION, etc.
- `event_category`: AUTHENTICATION, AUTHORIZATION, DATA, SYSTEM, SECURITY
- `details`: JSONB for flexible metadata
- System columns (`__tx_lsn`, `__curr_hash`, etc.) automatically added

### Step 2: Create Supporting Tables

```sql
-- Users table (regular table for demo)
CREATE TABLE users (
    user_id SERIAL PRIMARY KEY,
    username VARCHAR(100) UNIQUE NOT NULL,
    role VARCHAR(30) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Insert sample users
INSERT INTO users (username, role) VALUES
    ('dr_smith', 'DOCTOR'),
    ('nurse_jones', 'NURSE'),
    ('admin_wilson', 'ADMIN'),
    ('receptionist_lee', 'STAFF'),
    ('patient_service', 'SERVICE_ACCOUNT');
```

**Expected Output**:
```
CREATE TABLE
INSERT 0 5
```

## Logging Different Event Types

### Event Type 1: User Authentication

```sql
-- Successful login
INSERT INTO audit_log (
    event_type, event_category, user_id, username,
    action, details, ip_address, user_agent, success
) VALUES (
    'LOGIN',
    'AUTHENTICATION',
    1,
    'dr_smith',
    'USER_LOGIN',
    jsonb_build_object(
        'login_method', 'password',
        'mfa_used', true,
        'session_id', 'sess_' || md5(random()::text)
    ),
    '192.168.1.100',
    'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36',
    true
);

-- Failed login attempt
INSERT INTO audit_log (
    event_type, event_category, user_id, username,
    action, details, ip_address, success, error_message
) VALUES (
    'LOGIN_FAILED',
    'AUTHENTICATION',
    -1,
    'unknown_user',
    'USER_LOGIN_ATTEMPT',
    jsonb_build_object(
        'login_method', 'password',
        'failure_reason', 'invalid_credentials',
        'attempt_number', 3
    ),
    '203.0.113.45',
    false,
    'Invalid username or password'
);
```

**Expected Output**:
```
INSERT 0 1
INSERT 0 1
```

### Event Type 2: Data Access (HIPAA Compliance)

```sql
-- Patient record access
INSERT INTO audit_log (
    event_type, event_category, user_id, username,
    resource_type, resource_id, action, details, ip_address
) VALUES (
    'DATA_ACCESS',
    'DATA',
    1,
    'dr_smith',
    'PATIENT_RECORD',
    'PAT-12345',
    'VIEW_MEDICAL_RECORD',
    jsonb_build_object(
        'record_sections', ARRAY['demographics', 'medications', 'lab_results'],
        'access_reason', 'routine_checkup',
        'duration_seconds', 45,
        'printed', false
    ),
    '192.168.1.100'
);

-- PHI (Protected Health Information) export
INSERT INTO audit_log (
    event_type, event_category, user_id, username,
    resource_type, resource_id, action, details, ip_address
) VALUES (
    'DATA_EXPORT',
    'DATA',
    2,
    'nurse_jones',
    'PATIENT_RECORD',
    'PAT-12345',
    'EXPORT_PHI',
    jsonb_build_object(
        'export_format', 'PDF',
        'fields_exported', ARRAY['name', 'dob', 'diagnosis', 'medications'],
        'export_destination', 'local_download',
        'file_hash', md5(random()::text)
    ),
    '192.168.1.105'
);
```

**Expected Output**:
```
INSERT 0 1
INSERT 0 1
```

### Event Type 3: Data Modification

```sql
-- Update patient record
INSERT INTO audit_log (
    event_type, event_category, user_id, username,
    resource_type, resource_id, action, details, ip_address
) VALUES (
    'DATA_MODIFY',
    'DATA',
    1,
    'dr_smith',
    'PATIENT_RECORD',
    'PAT-12345',
    'UPDATE_MEDICATION',
    jsonb_build_object(
        'field_changed', 'medications',
        'old_value', 'Aspirin 81mg daily',
        'new_value', 'Aspirin 81mg daily, Lisinopril 10mg daily',
        'change_reason', 'New prescription for hypertension',
        'reviewed_by', 'dr_smith'
    ),
    '192.168.1.100'
);

-- Create new record
INSERT INTO audit_log (
    event_type, event_category, user_id, username,
    resource_type, resource_id, action, details, ip_address
) VALUES (
    'DATA_CREATE',
    'DATA',
    2,
    'nurse_jones',
    'PATIENT_RECORD',
    'PAT-99999',
    'CREATE_PATIENT',
    jsonb_build_object(
        'patient_name', 'John Doe',
        'admitted_by', 'nurse_jones',
        'admission_type', 'emergency',
        'initial_diagnosis', 'chest_pain'
    ),
    '192.168.1.105'
);
```

**Expected Output**:
```
INSERT 0 1
INSERT 0 1
```

### Event Type 4: Administrative Actions

```sql
-- User role change
INSERT INTO audit_log (
    event_type, event_category, user_id, username,
    resource_type, resource_id, action, details, ip_address
) VALUES (
    'ADMIN_ACTION',
    'SYSTEM',
    3,
    'admin_wilson',
    'USER',
    '4',
    'CHANGE_USER_ROLE',
    jsonb_build_object(
        'target_user', 'receptionist_lee',
        'old_role', 'STAFF',
        'new_role', 'SENIOR_STAFF',
        'reason', 'Promotion',
        'effective_date', CURRENT_DATE
    ),
    '192.168.1.110'
);

-- System configuration change
INSERT INTO audit_log (
    event_type, event_category, user_id, username,
    resource_type, resource_id, action, details, ip_address
) VALUES (
    'ADMIN_ACTION',
    'SYSTEM',
    3,
    'admin_wilson',
    'SYSTEM_CONFIG',
    'session_timeout',
    'UPDATE_CONFIG',
    jsonb_build_object(
        'setting_name', 'session_timeout_minutes',
        'old_value', '30',
        'new_value', '15',
        'reason', 'Security policy update - HIPAA compliance'
    ),
    '192.168.1.110'
);
```

**Expected Output**:
```
INSERT 0 1
INSERT 0 1
```

### Event Type 5: Security Events

```sql
-- Unauthorized access attempt
INSERT INTO audit_log (
    event_type, event_category, user_id, username,
    resource_type, resource_id, action, details, ip_address, success, error_message
) VALUES (
    'SECURITY_EVENT',
    'SECURITY',
    2,
    'nurse_jones',
    'PATIENT_RECORD',
    'PAT-54321',
    'UNAUTHORIZED_ACCESS_ATTEMPT',
    jsonb_build_object(
        'required_permission', 'VIEW_PSYCHIATRIC_RECORDS',
        'user_permissions', ARRAY['VIEW_GENERAL_RECORDS', 'UPDATE_VITALS'],
        'alert_sent', true,
        'alert_recipients', ARRAY['security@hospital.com']
    ),
    '192.168.1.105',
    false,
    'Insufficient permissions'
);

-- Suspicious activity
INSERT INTO audit_log (
    event_type, event_category, user_id, username,
    resource_type, action, details, ip_address
) VALUES (
    'SECURITY_EVENT',
    'SECURITY',
    1,
    'dr_smith',
    'PATIENT_RECORD',
    'BULK_ACCESS_DETECTED',
    jsonb_build_object(
        'records_accessed_count', 47,
        'time_window_minutes', 5,
        'threshold_exceeded', true,
        'automated_alert', true,
        'records_accessed', ARRAY['PAT-111', 'PAT-222', 'PAT-333']
    ),
    '192.168.1.100'
);
```

**Expected Output**:
```
INSERT 0 1
INSERT 0 1
```

## Querying the Audit Trail

### Query 1: Recent Activity by User

```sql
-- Show recent activity for specific user
SELECT
    __tx_lsn as log_sequence,
    event_type,
    action,
    resource_type,
    resource_id,
    success,
    timestamp,
    details->>'change_reason' as reason
FROM audit_log
WHERE username = 'dr_smith'
ORDER BY timestamp DESC
LIMIT 10;
```

**Expected Output**:
```
 log_sequence |   event_type   |          action          | resource_type |resource_id| success |          timestamp          |          reason
--------------+----------------+--------------------------+---------------+-----------+---------+-----------------------------+---------------------------
           11 | SECURITY_EVENT | BULK_ACCESS_DETECTED     | PATIENT_RECORD|           | t       | 2025-10-31 10:45:30.123456 |
            5 | DATA_MODIFY    | UPDATE_MEDICATION        | PATIENT_RECORD| PAT-12345 | t       | 2025-10-31 10:42:15.789012 | New prescription for...
            3 | DATA_ACCESS    | VIEW_MEDICAL_RECORD      | PATIENT_RECORD| PAT-12345 | t       | 2025-10-31 10:40:05.456789 |
            1 | LOGIN          | USER_LOGIN               |               |           | t       | 2025-10-31 10:35:00.123456 |
(4 rows)
```

### Query 2: All Access to Specific Resource

```sql
-- Complete audit trail for a patient record
SELECT
    timestamp,
    username,
    event_type,
    action,
    details->>'access_reason' as reason,
    ip_address,
    success
FROM audit_log
WHERE resource_type = 'PATIENT_RECORD'
  AND resource_id = 'PAT-12345'
ORDER BY timestamp;
```

**Expected Output**:
```
          timestamp          |  username   |  event_type  |       action        |      reason       |   ip_address    | success
-----------------------------+-------------+--------------+---------------------+-------------------+-----------------+---------
 2025-10-31 10:40:05.456789 | dr_smith    | DATA_ACCESS  | VIEW_MEDICAL_RECORD | routine_checkup   | 192.168.1.100  | t
 2025-10-31 10:41:20.654321 | nurse_jones | DATA_EXPORT  | EXPORT_PHI          |                   | 192.168.1.105  | t
 2025-10-31 10:42:15.789012 | dr_smith    | DATA_MODIFY  | UPDATE_MEDICATION   |                   | 192.168.1.100  | t
(3 rows)
```

### Query 3: Security Events and Failed Actions

```sql
-- All security events and failures
SELECT
    timestamp,
    event_type,
    username,
    action,
    ip_address,
    error_message,
    details
FROM audit_log
WHERE success = false
   OR event_category = 'SECURITY'
ORDER BY timestamp DESC;
```

**Expected Output**:
```
          timestamp          |    event_type    |   username    |           action                |   ip_address   |      error_message          |                details
-----------------------------+------------------+---------------+---------------------------------+----------------+-----------------------------+---------------------------------------
 2025-10-31 10:45:30.123456 | SECURITY_EVENT   | dr_smith      | BULK_ACCESS_DETECTED           | 192.168.1.100 |                             | {"records_accessed_count": 47, ...}
 2025-10-31 10:43:00.987654 | SECURITY_EVENT   | nurse_jones   | UNAUTHORIZED_ACCESS_ATTEMPT    | 192.168.1.105 | Insufficient permissions    | {"required_permission": "VIEW_...}
 2025-10-31 10:36:15.234567 | LOGIN_FAILED     | unknown_user  | USER_LOGIN_ATTEMPT             | 203.0.113.45  | Invalid username/password   | {"failure_reason": "invalid_...}
(3 rows)
```

### Query 4: Activity Summary by Event Type

```sql
-- Summary statistics by event type
SELECT
    event_type,
    event_category,
    COUNT(*) as event_count,
    COUNT(*) FILTER (WHERE success = false) as failure_count,
    COUNT(DISTINCT username) as unique_users,
    MIN(timestamp) as first_occurrence,
    MAX(timestamp) as last_occurrence
FROM audit_log
GROUP BY event_type, event_category
ORDER BY event_count DESC;
```

**Expected Output**:
```
    event_type    | event_category | event_count | failure_count | unique_users |     first_occurrence        |      last_occurrence
------------------+----------------+-------------+---------------+--------------+-----------------------------+-----------------------------
 DATA_ACCESS      | DATA           |           1 |             0 |            1 | 2025-10-31 10:40:05.456789 | 2025-10-31 10:40:05.456789
 DATA_MODIFY      | DATA           |           1 |             0 |            1 | 2025-10-31 10:42:15.789012 | 2025-10-31 10:42:15.789012
 SECURITY_EVENT   | SECURITY       |           2 |             1 |            2 | 2025-10-31 10:43:00.987654 | 2025-10-31 10:45:30.123456
 ADMIN_ACTION     | SYSTEM         |           2 |             0 |            1 | 2025-10-31 10:44:00.111111 | 2025-10-31 10:44:30.222222
 LOGIN            | AUTHENTICATION |           1 |             0 |            1 | 2025-10-31 10:35:00.123456 | 2025-10-31 10:35:00.123456
 LOGIN_FAILED     | AUTHENTICATION |           1 |             1 |            1 | 2025-10-31 10:36:15.234567 | 2025-10-31 10:36:15.234567
(6 rows)
```

### Query 5: User Activity Timeline

```sql
-- Detailed timeline for compliance investigation
SELECT
    __tx_lsn as seq,
    TO_CHAR(timestamp, 'HH24:MI:SS.MS') as time,
    username,
    event_type,
    action,
    resource_id,
    CASE
        WHEN success THEN '✓'
        ELSE '✗'
    END as status,
    COALESCE(details->>'access_reason', details->>'change_reason', '') as reason
FROM audit_log
WHERE timestamp >= CURRENT_DATE
ORDER BY timestamp;
```

**Expected Output**:
```
 seq |      time       |   username    |    event_type    |          action          | resource_id | status |          reason
-----+-----------------+---------------+------------------+--------------------------+-------------+--------+---------------------------
   1 | 10:35:00.123    | dr_smith      | LOGIN            | USER_LOGIN               |             | ✓      |
   2 | 10:36:15.234    | unknown_user  | LOGIN_FAILED     | USER_LOGIN_ATTEMPT       |             | ✗      |
   3 | 10:40:05.456    | dr_smith      | DATA_ACCESS      | VIEW_MEDICAL_RECORD      | PAT-12345   | ✓      | routine_checkup
   4 | 10:41:20.654    | nurse_jones   | DATA_EXPORT      | EXPORT_PHI               | PAT-12345   | ✓      |
   5 | 10:42:15.789    | dr_smith      | DATA_MODIFY      | UPDATE_MEDICATION        | PAT-12345   | ✓      | New prescription for...
   ...
(11 rows)
```

## Compliance Reporting

### HIPAA Access Report

```sql
-- HIPAA-compliant access report for patient
SELECT
    timestamp as access_timestamp,
    username as accessor,
    action,
    details->>'access_reason' as access_reason,
    details->>'duration_seconds' as duration,
    ip_address,
    __tx_lsn as immutable_log_sequence,
    LEFT(encode(__curr_hash, 'hex'), 16) || '...' as verification_hash
FROM audit_log
WHERE resource_type = 'PATIENT_RECORD'
  AND resource_id = 'PAT-12345'
  AND event_type IN ('DATA_ACCESS', 'DATA_MODIFY', 'DATA_EXPORT')
ORDER BY timestamp;
```

**Expected Output**:
```
    access_timestamp        |   accessor  |       action        | access_reason   | duration |   ip_address   | immutable_log_sequence | verification_hash
----------------------------+-------------+---------------------+-----------------+----------+----------------+------------------------+-------------------
 2025-10-31 10:40:05.456789| dr_smith    | VIEW_MEDICAL_RECORD | routine_checkup | 45       | 192.168.1.100 |                      3 | 7a3f91c4e5d2b8f6...
 2025-10-31 10:41:20.654321| nurse_jones | EXPORT_PHI          |                 |          | 192.168.1.105 |                      4 | 9b2e4f6a8c1d3e5f...
 2025-10-31 10:42:15.789012| dr_smith    | UPDATE_MEDICATION   |                 |          | 192.168.1.100 |                      5 | 4c6e8f1a3b5c7d9e...
(3 rows)
```

**Explanation**: This report provides tamper-evident proof of who accessed patient data, when, and why - required for HIPAA compliance.

### Monthly Security Summary

```sql
-- Monthly security and compliance summary
SELECT
    DATE_TRUNC('day', timestamp) as date,
    COUNT(*) FILTER (WHERE event_category = 'AUTHENTICATION') as auth_events,
    COUNT(*) FILTER (WHERE event_category = 'DATA') as data_events,
    COUNT(*) FILTER (WHERE event_category = 'SECURITY') as security_events,
    COUNT(*) FILTER (WHERE success = false) as failed_events,
    COUNT(DISTINCT username) as active_users,
    COUNT(DISTINCT ip_address) as unique_ips
FROM audit_log
WHERE timestamp >= DATE_TRUNC('month', CURRENT_DATE)
GROUP BY DATE_TRUNC('day', timestamp)
ORDER BY date;
```

**Expected Output**:
```
         date         | auth_events | data_events | security_events | failed_events | active_users | unique_ips
----------------------+-------------+-------------+-----------------+---------------+--------------+------------
 2025-10-31 00:00:00 |           2 |           3 |               2 |             2 |            4 |          3
(1 row)
```

## Retention Policies

### Archive Old Logs (Conceptual)

```sql
-- Note: Blockchain tables don't support DELETE, so archival means:
-- 1. Export to archive storage
-- 2. Create new blockchain table
-- 3. Drop old table after verification

-- Step 1: Export logs older than 7 years (HIPAA requirement)
COPY (
    SELECT * FROM audit_log
    WHERE timestamp < CURRENT_DATE - INTERVAL '7 years'
    ORDER BY __tx_lsn
) TO '/archive/audit_log_2018.csv' CSV HEADER;

-- Step 2: Verify export
-- (manual verification process)

-- Step 3: For new system, create fresh table
-- DROP TABLE audit_log;
-- CREATE BLOCKCHAIN TABLE audit_log (...);
```

**Important**: Blockchain tables are immutable. For retention policies:
- Export old data to secure archive
- Maintain blockchain integrity by keeping full chain
- Consider table partitioning for large datasets (future enhancement)

### Query Historical Archive

```sql
-- Query to show retention status
SELECT
    '< 1 year' as retention_period,
    COUNT(*) as log_count,
    MIN(timestamp) as oldest_log,
    MAX(timestamp) as newest_log
FROM audit_log
WHERE timestamp >= CURRENT_DATE - INTERVAL '1 year'

UNION ALL

SELECT
    '1-3 years',
    COUNT(*),
    MIN(timestamp),
    MAX(timestamp)
FROM audit_log
WHERE timestamp BETWEEN CURRENT_DATE - INTERVAL '3 years'
                    AND CURRENT_DATE - INTERVAL '1 year'

UNION ALL

SELECT
    '3-7 years',
    COUNT(*),
    MIN(timestamp),
    MAX(timestamp)
FROM audit_log
WHERE timestamp BETWEEN CURRENT_DATE - INTERVAL '7 years'
                    AND CURRENT_DATE - INTERVAL '3 years';
```

**Expected Output**:
```
 retention_period | log_count |       oldest_log        |       newest_log
------------------+-----------+-------------------------+-------------------------
 < 1 year        |        11 | 2025-10-31 10:35:00.123| 2025-10-31 10:45:30.123
 1-3 years       |         0 |                         |
 3-7 years       |         0 |                         |
(3 rows)
```

## Verify Audit Log Integrity

### Comprehensive Verification

```sql
-- Verify audit log blockchain integrity
WITH RECURSIVE hash_chain AS (
    SELECT __tx_lsn, __curr_hash, __prev_hash, 1 as depth
    FROM audit_log
    WHERE __prev_hash = '\x0000000000000000000000000000000000000000000000000000000000000000'

    UNION ALL

    SELECT al.__tx_lsn, al.__curr_hash, al.__prev_hash, hc.depth + 1
    FROM audit_log al
    INNER JOIN hash_chain hc ON al.__prev_hash = hc.__curr_hash
)
SELECT
    (SELECT COUNT(*) FROM audit_log) as total_logs,
    COUNT(*) as verified_logs,
    (SELECT COUNT(*) FROM audit_log) - COUNT(*) as broken_chain,
    CASE
        WHEN (SELECT COUNT(*) FROM audit_log) = COUNT(*) THEN 'PASS: Audit trail intact'
        ELSE 'FAIL: Chain corruption detected'
    END as audit_status
FROM hash_chain;
```

**Expected Output**:
```
 total_logs | verified_logs | broken_chain |      audit_status
------------+---------------+--------------+------------------------
         11 |            11 |            0 | PASS: Audit trail intact
(1 row)
```

## Performance Optimization

### Create Indexes (Not Supported on Blockchain Tables)

```sql
-- Note: Blockchain tables don't support indexes
-- For query performance, consider:
-- 1. Query by __tx_lsn (already sequential)
-- 2. Filter by timestamp (inherent ordering)
-- 3. Use materialized views for complex queries
```

### Materialized View for Dashboards

```sql
-- Create materialized view for quick dashboard queries
CREATE MATERIALIZED VIEW audit_log_summary AS
SELECT
    DATE_TRUNC('hour', timestamp) as hour,
    event_type,
    event_category,
    COUNT(*) as event_count,
    COUNT(DISTINCT username) as unique_users,
    COUNT(*) FILTER (WHERE success = false) as failures
FROM audit_log
GROUP BY DATE_TRUNC('hour', timestamp), event_type, event_category;

-- Refresh periodically
REFRESH MATERIALIZED VIEW audit_log_summary;

-- Query the summary (fast)
SELECT * FROM audit_log_summary
WHERE hour >= CURRENT_DATE
ORDER BY hour DESC, event_count DESC;
```

**Expected Output**:
```
         hour         |    event_type    | event_category | event_count | unique_users | failures
----------------------+------------------+----------------+-------------+--------------+----------
 2025-10-31 10:00:00 | DATA_ACCESS      | DATA           |           1 |            1 |        0
 2025-10-31 10:00:00 | SECURITY_EVENT   | SECURITY       |           2 |            2 |        1
 2025-10-31 10:00:00 | ADMIN_ACTION     | SYSTEM         |           2 |            1 |        0
 2025-10-31 10:00:00 | LOGIN            | AUTHENTICATION |           1 |            1 |        0
 2025-10-31 10:00:00 | LOGIN_FAILED     | AUTHENTICATION |           1 |            1 |        1
(5 rows)
```

## Cleanup

```sql
-- Drop test tables (blockchain table cannot be modified, only dropped)
DROP TABLE audit_log;
DROP TABLE users;
DROP MATERIALIZED VIEW IF EXISTS audit_log_summary;
```

## Key Takeaways

1. **Immutable Audit Trail**: Blockchain tables provide tamper-evident logging perfect for compliance (HIPAA, SOX, GDPR, PCI-DSS).

2. **Rich Metadata**: JSONB details field allows flexible event-specific data without schema changes.

3. **Query Patterns**: Filter by user, resource, time range, and event type for comprehensive audit trails.

4. **Compliance Ready**: Built-in hash chain verification proves logs haven't been tampered with.

5. **Performance**: Use materialized views for dashboard queries, direct blockchain queries for detailed investigation.

6. **Retention**: Export old data for archival, but maintain full blockchain for integrity verification.

7. **Real-World Application**: This pattern works for:
   - Healthcare (HIPAA)
   - Finance (SOX, PCI-DSS)
   - Government (NIST, FedRAMP)
   - SaaS (SOC 2, ISO 27001)

8. **Zero Trust**: Even database administrators cannot modify or delete audit logs without detection.

## Related Examples

- [Query Anchoring](query-anchoring.md) - Anchor compliance reports
- [Verification](verification.md) - Verify audit trail integrity
- [Financial Records](financial-records.md) - Similar pattern for financial audit
- [Troubleshooting](troubleshooting.md) - Debug audit logging issues
