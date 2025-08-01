#!/bin/bash

# PostgreSQL Blockchain Build Script - Simplified Version
# This script performs the complete build cycle from scratch

set -e  # Exit on any error

# Configuration
PGSQL_PREFIX="$HOME/pgsql"
PG_DATA="$PGSQL_PREFIX/data"
ICU_PREFIX=$(brew --prefix icu4c)
BUILD_DIR="/Users/aditya/Documents/Github/apostgres"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log() {
    echo -e "${BLUE}[$(date '+%Y-%m-%d %H:%M:%S')] $1${NC}"
}

success() {
    echo -e "${GREEN}✅ $1${NC}"
}

error() {
    echo -e "${RED}❌ $1${NC}"
    exit 1
}

# Step 1: Clean and prepare
log "=== STEP 1: CLEANUP ==="
pkill -f postgres || true
sleep 2
rm -rf "$PGSQL_PREFIX"
cd "$BUILD_DIR"
make clean || true

# Step 2: Configure and build
log "=== STEP 2: BUILD ==="
export PKG_CONFIG_PATH="$ICU_PREFIX/lib/pkgconfig:$PKG_CONFIG_PATH"
export CPPFLAGS="-I$ICU_PREFIX/include"
export LDFLAGS="-L$ICU_PREFIX/lib -Wl,-rpath,$PGSQL_PREFIX/lib"

./configure \
    --prefix="$PGSQL_PREFIX" \
    --with-icu \
    CFLAGS='-O0 -g' \
    --enable-debug \
    --enable-cassert \
    --enable-depend

make -j$(sysctl -n hw.ncpu)
make install

success "PostgreSQL build completed"

# Step 3: Initialize database
log "=== STEP 3: INITIALIZE DATABASE ==="
rm -rf "$PG_DATA"
"$PGSQL_PREFIX/bin/initdb" -D "$PG_DATA" --auth-local=trust --auth-host=md5

success "Database initialized"

# Step 4: Start server
log "=== STEP 4: START SERVER ==="
"$PGSQL_PREFIX/bin/pg_ctl" -D "$PG_DATA" start
sleep 5

success "Server started"

# Step 5: Load advanced blockchain functions
log "=== STEP 5: LOAD BLOCKCHAIN FUNCTIONS ==="
"$PGSQL_PREFIX/bin/psql" -d postgres -f "$BUILD_DIR/src/backend/catalog/blockchain_functions.sql"

success "Blockchain functions loaded"

# Step 6: Test basic functionality
log "=== STEP 6: BASIC FUNCTIONALITY TEST ==="
"$PGSQL_PREFIX/bin/psql" -d postgres -c "
    CREATE BLOCKCHAIN TABLE quick_test (id int, data text);
    INSERT INTO quick_test VALUES (1, 'hello'), (2, 'world');
    SELECT 'Test Results:' as label;
    SELECT id, data, __tx_lsn FROM quick_test;
    SELECT 'Functions:' as label;
    SELECT is_blockchain_table('quick_test') as is_blockchain, verify_blockchain_chain('quick_test') as chain_valid;
"

success "Basic functionality test passed"

# Step 7: Run comprehensive tests
log "=== STEP 7: COMPREHENSIVE TESTS ==="
"$PGSQL_PREFIX/bin/psql" -d postgres -f "$BUILD_DIR/blockchain_comprehensive_tests.sql" > /tmp/comprehensive_test.log 2>&1

# Check critical results
if grep -q "CREATE TABLE" /tmp/comprehensive_test.log && \
   grep -q "INSERT 0" /tmp/comprehensive_test.log && \
   grep -q "Total rows in blockchain table:" /tmp/comprehensive_test.log; then
    success "Comprehensive tests passed"
else
    error "Comprehensive tests failed - check /tmp/comprehensive_test.log"
fi

# Step 8: Test counter persistence
log "=== STEP 8: COUNTER PERSISTENCE TEST ==="
max_counter=$("$PGSQL_PREFIX/bin/psql" -d postgres -t -c "SELECT COALESCE(MAX(__tx_lsn), 0) FROM blockchain_audit_log;" | xargs)
log "Current max counter: $max_counter"

"$PGSQL_PREFIX/bin/pg_ctl" -D "$PG_DATA" restart
sleep 5

"$PGSQL_PREFIX/bin/psql" -d postgres -c "INSERT INTO blockchain_audit_log (user_id, user_name, email, action, ip_address) VALUES (88888, 'persistence_test', 'test@example.com', 'test', '127.0.0.1');"

new_counter=$("$PGSQL_PREFIX/bin/psql" -d postgres -t -c "SELECT __tx_lsn FROM blockchain_audit_log WHERE user_id = 88888;" | xargs)
expected_counter=$((max_counter + 1))

if [ "$new_counter" = "$expected_counter" ]; then
    success "Counter persistence test passed: $max_counter -> $new_counter"
else
    error "Counter persistence test failed: expected $expected_counter, got $new_counter"
fi

# Final status
log "=== FINAL STATUS ==="
table_count=$("$PGSQL_PREFIX/bin/psql" -d postgres -t -c "SELECT COUNT(*) FROM list_blockchain_tables();" | xargs)
total_rows=$("$PGSQL_PREFIX/bin/psql" -d postgres -t -c "SELECT COUNT(*) FROM blockchain_audit_log;" | xargs)

success "Build and test completed successfully!"
log "Blockchain tables: $table_count"
log "Total blockchain rows: $total_rows"
log ""
log "To connect: $PGSQL_PREFIX/bin/psql -d postgres"
log "To stop: $PGSQL_PREFIX/bin/pg_ctl -D $PG_DATA stop"

exit 0