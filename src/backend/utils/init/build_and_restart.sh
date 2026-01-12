#!/bin/bash
set -e

echo "=== Building PostgreSQL ==="
make -j4

echo ""
echo "=== Installing ==="
make install

echo ""
echo "=== Restarting PostgreSQL ==="
~/pgsql/bin/pg_ctl restart -D ~/pgsql/data -l ~/pgsql/data/logfile -w

echo ""
echo "=== Build and restart complete ==="
