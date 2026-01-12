-- Maximum TPS Test for Single Client
-- Tests various transaction sizes to find optimal throughput

\timing on

-- Test 1: Individual 1-row transactions (max TPS)
DROP BLOCKCHAIN TABLE IF EXISTS tps_test_1row CASCADE;
CREATE BLOCKCHAIN TABLE tps_test_1row (test_id INT, data TEXT);

\echo '=== Test 1: 1,000 individual 1-row transactions ==='
\echo 'Measuring pure transaction throughput (TPS)...'
DO $$
DECLARE
    start_time TIMESTAMP;
    end_time TIMESTAMP;
    elapsed_sec NUMERIC;
    tps NUMERIC;
BEGIN
    start_time := clock_timestamp();

    FOR i IN 1..1000 LOOP
        BEGIN
            INSERT INTO tps_test_1row VALUES (i, 'data_' || i);
            COMMIT;
        END;
    END LOOP;

    end_time := clock_timestamp();
    elapsed_sec := EXTRACT(EPOCH FROM (end_time - start_time));
    tps := 1000.0 / elapsed_sec;

    RAISE NOTICE 'Elapsed: % seconds, TPS: %', ROUND(elapsed_sec, 2), ROUND(tps, 2);
END $$;

SELECT COUNT(*) as total_rows FROM tps_test_1row;

-- Test 2: 10-row batch transactions
DROP BLOCKCHAIN TABLE IF EXISTS tps_test_10row CASCADE;
CREATE BLOCKCHAIN TABLE tps_test_10row (test_id INT, data TEXT);

\echo ''
\echo '=== Test 2: 100 transactions × 10 rows each ==='
\echo 'Batch size: 10 rows per transaction'
DO $$
DECLARE
    start_time TIMESTAMP;
    end_time TIMESTAMP;
    elapsed_sec NUMERIC;
    tps NUMERIC;
    rows_per_sec NUMERIC;
BEGIN
    start_time := clock_timestamp();

    FOR i IN 0..99 LOOP
        BEGIN
            INSERT INTO tps_test_10row
            SELECT j, 'data_' || j
            FROM generate_series(i * 10 + 1, (i + 1) * 10) j;
            COMMIT;
        END;
    END LOOP;

    end_time := clock_timestamp();
    elapsed_sec := EXTRACT(EPOCH FROM (end_time - start_time));
    tps := 100.0 / elapsed_sec;
    rows_per_sec := 1000.0 / elapsed_sec;

    RAISE NOTICE 'Elapsed: % seconds, TPS: %, Rows/sec: %',
                 ROUND(elapsed_sec, 2), ROUND(tps, 2), ROUND(rows_per_sec, 2);
END $$;

SELECT COUNT(*) as total_rows FROM tps_test_10row;

-- Test 3: 100-row batch transactions
DROP BLOCKCHAIN TABLE IF EXISTS tps_test_100row CASCADE;
CREATE BLOCKCHAIN TABLE tps_test_100row (test_id INT, data TEXT);

\echo ''
\echo '=== Test 3: 100 transactions × 100 rows each ==='
\echo 'Batch size: 100 rows per transaction'
DO $$
DECLARE
    start_time TIMESTAMP;
    end_time TIMESTAMP;
    elapsed_sec NUMERIC;
    tps NUMERIC;
    rows_per_sec NUMERIC;
BEGIN
    start_time := clock_timestamp();

    FOR i IN 0..99 LOOP
        BEGIN
            INSERT INTO tps_test_100row
            SELECT j, 'data_' || j
            FROM generate_series(i * 100 + 1, (i + 1) * 100) j;
            COMMIT;
        END;
    END LOOP;

    end_time := clock_timestamp();
    elapsed_sec := EXTRACT(EPOCH FROM (end_time - start_time));
    tps := 100.0 / elapsed_sec;
    rows_per_sec := 10000.0 / elapsed_sec;

    RAISE NOTICE 'Elapsed: % seconds, TPS: %, Rows/sec: %',
                 ROUND(elapsed_sec, 2), ROUND(tps, 2), ROUND(rows_per_sec, 2);
END $$;

SELECT COUNT(*) as total_rows FROM tps_test_100row;

-- Test 4: 1,000-row batch transactions
DROP BLOCKCHAIN TABLE IF EXISTS tps_test_1000row CASCADE;
CREATE BLOCKCHAIN TABLE tps_test_1000row (test_id INT, data TEXT);

\echo ''
\echo '=== Test 4: 10 transactions × 1,000 rows each ==='
\echo 'Batch size: 1,000 rows per transaction'
DO $$
DECLARE
    start_time TIMESTAMP;
    end_time TIMESTAMP;
    elapsed_sec NUMERIC;
    tps NUMERIC;
    rows_per_sec NUMERIC;
BEGIN
    start_time := clock_timestamp();

    FOR i IN 0..9 LOOP
        BEGIN
            INSERT INTO tps_test_1000row
            SELECT j, 'data_' || j
            FROM generate_series(i * 1000 + 1, (i + 1) * 1000) j;
            COMMIT;
        END;
    END LOOP;

    end_time := clock_timestamp();
    elapsed_sec := EXTRACT(EPOCH FROM (end_time - start_time));
    tps := 10.0 / elapsed_sec;
    rows_per_sec := 10000.0 / elapsed_sec;

    RAISE NOTICE 'Elapsed: % seconds, TPS: %, Rows/sec: %',
                 ROUND(elapsed_sec, 2), ROUND(tps, 2), ROUND(rows_per_sec, 2);
END $$;

SELECT COUNT(*) as total_rows FROM tps_test_1000row;

\echo ''
\echo '=== Summary ==='
\echo 'Optimal batch size provides best rows/sec throughput'
\echo 'Smaller batches provide higher TPS but lower total throughput'
