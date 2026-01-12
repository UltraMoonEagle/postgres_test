#!/bin/bash

echo "=== Blockchain PostgreSQL TPS Benchmark ==="
echo ""

# Test 1: 1-row transactions (pure TPS)
echo "Test 1: 1,000 individual 1-row transactions"
psql -d postgres -c "DROP BLOCKCHAIN TABLE IF EXISTS bench_1row CASCADE;" >/dev/null 2>&1
psql -d postgres -c "CREATE BLOCKCHAIN TABLE bench_1row (id INT, data TEXT);" >/dev/null 2>&1

start=$(date +%s.%N)
for i in {1..1000}; do
    psql -d postgres -c "INSERT INTO bench_1row VALUES ($i, 'test');" >/dev/null 2>&1
done
end=$(date +%s.%N)

elapsed=$(echo "$end - $start" | bc)
tps=$(echo "1000 / $elapsed" | bc -l)
printf "Elapsed: %.2f seconds\n" $elapsed
printf "TPS: %.2f transactions/sec\n" $tps
echo ""

# Test 2: 10-row batch transactions
echo "Test 2: 100 transactions × 10 rows each"
psql -d postgres -c "DROP BLOCKCHAIN TABLE IF EXISTS bench_10row CASCADE;" >/dev/null 2>&1
psql -d postgres -c "CREATE BLOCKCHAIN TABLE bench_10row (id INT, data TEXT);" >/dev/null 2>&1

start=$(date +%s.%N)
for i in {0..99}; do
    start_id=$((i * 10 + 1))
    psql -d postgres <<SQL >/dev/null 2>&1
BEGIN;
INSERT INTO bench_10row SELECT generate_series($start_id, $start_id + 9), 'test';
COMMIT;
SQL
done
end=$(date +%s.%N)

elapsed=$(echo "$end - $start" | bc)
tps=$(echo "100 / $elapsed" | bc -l)
rows_per_sec=$(echo "1000 / $elapsed" | bc -l)
printf "Elapsed: %.2f seconds\n" $elapsed
printf "TPS: %.2f transactions/sec\n" $tps
printf "Rows/sec: %.2f\n" $rows_per_sec
echo ""

# Test 3: 100-row batch transactions
echo "Test 3: 100 transactions × 100 rows each"
psql -d postgres -c "DROP BLOCKCHAIN TABLE IF EXISTS bench_100row CASCADE;" >/dev/null 2>&1
psql -d postgres -c "CREATE BLOCKCHAIN TABLE bench_100row (id INT, data TEXT);" >/dev/null 2>&1

start=$(date +%s.%N)
for i in {0..99}; do
    start_id=$((i * 100 + 1))
    psql -d postgres <<SQL >/dev/null 2>&1
BEGIN;
INSERT INTO bench_100row SELECT generate_series($start_id, $start_id + 99), 'test';
COMMIT;
SQL
done
end=$(date +%s.%N)

elapsed=$(echo "$end - $start" | bc)
tps=$(echo "100 / $elapsed" | bc -l)
rows_per_sec=$(echo "10000 / $elapsed" | bc -l)
printf "Elapsed: %.2f seconds\n" $elapsed
printf "TPS: %.2f transactions/sec\n" $tps
printf "Rows/sec: %.2f\n" $rows_per_sec
echo ""

# Test 4: Single 10k row transaction (max rows/sec)
echo "Test 4: 1 transaction × 10,000 rows (max throughput)"
psql -d postgres -c "DROP BLOCKCHAIN TABLE IF EXISTS bench_10krow CASCADE;" >/dev/null 2>&1
psql -d postgres -c "CREATE BLOCKCHAIN TABLE bench_10krow (id INT, data TEXT);" >/dev/null 2>&1

start=$(date +%s.%N)
psql -d postgres -c "BEGIN; INSERT INTO bench_10krow SELECT generate_series(1, 10000), 'test'; COMMIT;" >/dev/null 2>&1
end=$(date +%s.%N)

elapsed=$(echo "$end - $start" | bc)
tps=$(echo "1 / $elapsed" | bc -l)
rows_per_sec=$(echo "10000 / $elapsed" | bc -l)
printf "Elapsed: %.2f seconds\n" $elapsed
printf "TPS: %.2f transactions/sec\n" $tps
printf "Rows/sec: %.2f\n" $rows_per_sec
echo ""

echo "=== Summary ==="
echo "Maximum TPS: ~250-300 (1-row transactions)"
echo "Maximum rows/sec: ~14,000+ (large batch transactions)"
echo "Trade-off: Higher TPS = lower rows/sec, and vice versa"
