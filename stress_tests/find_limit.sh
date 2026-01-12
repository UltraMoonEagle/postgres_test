#!/bin/bash

for rows in 500 1000 2000 3000 5000 7500 10000; do
  echo "=== Testing $rows rows ==="
  psql -d postgres -c "DROP BLOCKCHAIN TABLE IF EXISTS test_limit CASCADE;" \
                    -c "CREATE BLOCKCHAIN TABLE test_limit (test_id INT, data TEXT);" \
                    -c "BEGIN; INSERT INTO test_limit (test_id, data) SELECT i, 'data_' || i FROM generate_series(1, $rows) i; COMMIT;" \
                    -c "SELECT COUNT(*) as inserted FROM test_limit;" 2>&1 | grep -E "(ERROR|inserted)" | head -2
  echo ""
done
