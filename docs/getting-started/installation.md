# Installation Guide

**One-minute summary:**

- Build PostgreSQL with blockchain extension from source
- Initialize a new database cluster with `initdb`
- Blockchain functionality is built-in (no `CREATE EXTENSION` required)
- Typical build time: 5-10 minutes on modern hardware

---

## Prerequisites

### System Requirements

| Component | Minimum | Recommended |
|-----------|---------|-------------|
| **OS** | Linux 3.10+, macOS 10.14+ | Ubuntu 20.04+ LTS, RHEL 8+ |
| **CPU** | 2 cores | 4+ cores |
| **RAM** | 2 GB | 4+ GB |
| **Disk** | 1 GB free | 5+ GB free |
| **Compiler** | GCC 4.9+ or Clang 3.8+ | GCC 9+ or Clang 11+ |

### Required Dependencies

=== "Ubuntu/Debian"

    ```bash
    sudo apt-get update
    sudo apt-get install -y \
        build-essential \
        libreadline-dev \
        zlib1g-dev \
        flex \
        bison \
        libxml2-dev \
        libxslt1-dev \
        libssl-dev \
        libperl-dev \
        python3-dev \
        uuid-dev
    ```

=== "RHEL/CentOS/Fedora"

    ```bash
    sudo yum install -y \
        gcc \
        make \
        readline-devel \
        zlib-devel \
        flex \
        bison \
        libxml2-devel \
        libxslt-devel \
        openssl-devel \
        perl-devel \
        python3-devel \
        libuuid-devel
    ```

=== "macOS"

    ```bash
    # Install Xcode Command Line Tools
    xcode-select --install

    # Install dependencies via Homebrew
    brew install \
        readline \
        zlib \
        flex \
        bison \
        libxml2 \
        libxslt \
        openssl@3 \
        perl \
        python@3.11
    ```

---

## Installation Steps

### Step 1: Download Source Code

```bash
# Clone the repository
git clone https://github.com/your-org/postgres_blockchain.git
cd postgres_blockchain

# Or download a specific release
wget https://github.com/your-org/postgres_blockchain/archive/refs/tags/v1.0.tar.gz
tar -xzf v1.0.tar.gz
cd postgres_blockchain-1.0
```

### Step 2: Configure Build

```bash
# Configure with default options
./configure --prefix=/usr/local/pgsql

# Or with custom installation directory
./configure --prefix=$HOME/pgsql

# Advanced: Enable debugging symbols
./configure --prefix=/usr/local/pgsql --enable-debug --enable-cassert
```

**Common configuration options:**

| Option | Purpose |
|--------|---------|
| `--prefix=PATH` | Installation directory (default: `/usr/local/pgsql`) |
| `--enable-debug` | Enable debugging symbols for development |
| `--enable-cassert` | Enable assertion checks (slower, for development) |
| `--with-openssl` | Enable OpenSSL support for additional crypto |
| `--with-perl` | Enable Perl stored procedures |
| `--with-python` | Enable Python stored procedures |

### Step 3: Compile PostgreSQL

```bash
# Build with parallel jobs (faster)
make -j$(nproc)

# Or specify number of parallel jobs manually
make -j4
```

**Build time estimates:**

- 2 cores: ~10-15 minutes
- 4 cores: ~5-8 minutes
- 8+ cores: ~3-5 minutes

### Step 4: Install Binaries

```bash
# Install to configured prefix
sudo make install

# Or without sudo if installing to home directory
make install
```

### Step 5: Add to PATH

Add PostgreSQL binaries to your PATH:

```bash
# For bash (~/.bashrc)
echo 'export PATH=/usr/local/pgsql/bin:$PATH' >> ~/.bashrc
echo 'export LD_LIBRARY_PATH=/usr/local/pgsql/lib:$LD_LIBRARY_PATH' >> ~/.bashrc
source ~/.bashrc

# For zsh (~/.zshrc)
echo 'export PATH=/usr/local/pgsql/bin:$PATH' >> ~/.zshrc
echo 'export LD_LIBRARY_PATH=/usr/local/pgsql/lib:$LD_LIBRARY_PATH' >> ~/.zshrc
source ~/.zshrc
```

### Step 6: Initialize Database Cluster

```bash
# Create data directory
mkdir -p $HOME/pgdata

# Initialize database cluster
initdb -D $HOME/pgdata

# The blockchain functionality is now built-in!
```

**Expected output:**

```
The files belonging to this database system will be owned by user "youruser".
This user must also own the server process.

The database cluster will be initialized with locale "en_US.UTF-8".
The default database encoding has accordingly been set to "UTF8".

creating directory /home/youruser/pgdata ... ok
creating subdirectories ... ok
selecting dynamic shared memory implementation ... posix
selecting default max_connections ... 100
selecting default shared_buffers ... 128MB
...
Success. You can now start the database server using:

    pg_ctl -D /home/youruser/pgdata -l logfile start
```

### Step 7: Start PostgreSQL Server

```bash
# Start the server
pg_ctl -D $HOME/pgdata -l logfile start

# Verify it's running
pg_ctl -D $HOME/pgdata status
```

**Expected output:**

```
pg_ctl: server is running (PID: 12345)
/usr/local/pgsql/bin/postgres "-D" "/home/youruser/pgdata"
```

### Step 8: Create Your First Database

```bash
# Create a database
createdb mydb

# Connect to it
psql mydb
```

### Step 9: Verify Blockchain Functionality

```sql
-- Check that blockchain table access method is available
SELECT amname, amhandler
FROM pg_am
WHERE amname = 'blockchain';

-- Expected output:
--   amname    |       amhandler
-- ------------+------------------------
--  blockchain | blockchainam_handler
```

**If you see the above output, installation is successful!** ✅

---

## Post-Installation Configuration

### Recommended postgresql.conf Settings

Edit `$PGDATA/postgresql.conf`:

```ini
# Memory settings for blockchain operations
shared_buffers = 256MB              # Minimum 128MB
work_mem = 16MB                     # For hash computations
maintenance_work_mem = 128MB        # For index operations

# WAL settings (important for crash recovery)
wal_level = replica                 # Or higher
wal_buffers = 16MB
checkpoint_timeout = 15min
max_wal_size = 2GB

# Logging (useful for debugging)
log_destination = 'stderr'
logging_collector = on
log_directory = 'log'
log_filename = 'postgresql-%Y-%m-%d_%H%M%S.log'
log_statement = 'ddl'               # Log DDL statements
log_duration = on
log_line_prefix = '%t [%p]: [%l-1] user=%u,db=%d,app=%a,client=%h '

# Performance
random_page_cost = 1.1              # For SSD storage
effective_cache_size = 4GB          # Set to ~50% of RAM
```

### Create System User (Production)

For production deployments, run PostgreSQL as a dedicated user:

```bash
# Create postgres user
sudo useradd -m -d /var/lib/postgresql -s /bin/bash postgres

# Create data directory
sudo mkdir -p /var/lib/postgresql/data
sudo chown -R postgres:postgres /var/lib/postgresql

# Switch to postgres user
sudo -u postgres bash

# Initialize as postgres user
/usr/local/pgsql/bin/initdb -D /var/lib/postgresql/data
```

### Set Up Systemd Service (Optional)

Create `/etc/systemd/system/postgresql.service`:

```ini
[Unit]
Description=PostgreSQL with Blockchain Extension
Documentation=https://your-docs-site.com
After=network.target

[Service]
Type=forking
User=postgres
Group=postgres
Environment=PGDATA=/var/lib/postgresql/data
ExecStart=/usr/local/pgsql/bin/pg_ctl start -D ${PGDATA} -l /var/lib/postgresql/logfile
ExecStop=/usr/local/pgsql/bin/pg_ctl stop -D ${PGDATA}
ExecReload=/usr/local/pgsql/bin/pg_ctl reload -D ${PGDATA}
TimeoutSec=300

[Install]
WantedBy=multi-user.target
```

Enable and start:

```bash
sudo systemctl daemon-reload
sudo systemctl enable postgresql
sudo systemctl start postgresql
sudo systemctl status postgresql
```

---

## Verification Checklist

Run these commands to verify your installation:

### 1. Binary Versions

```bash
postgres --version
psql --version
```

### 2. Blockchain Access Method

```sql
psql -d postgres -c "SELECT amname FROM pg_am WHERE amname = 'blockchain';"
```

**Expected:** One row with `blockchain`

### 3. Blockchain Functions

```sql
psql -d postgres -c "
SELECT proname, prorettype::regtype
FROM pg_proc
WHERE proname IN ('create_anchor_table', 'anchor_query_result', 'verify_query_anchor')
ORDER BY proname;
"
```

**Expected:** 3 rows listing the query anchoring functions

### 4. Create Test Blockchain Table

```sql
psql -d postgres -c "
CREATE BLOCKCHAIN TABLE test_chain (
    id SERIAL,
    data TEXT
);

INSERT INTO test_chain (data) VALUES ('Hello Blockchain!');

SELECT id, data, __tx_lsn, __tx_timestamp FROM test_chain;
"
```

**Expected:** One row showing your data plus blockchain system columns

---

## Troubleshooting

### Build Failures

**Problem:** `configure: error: readline library not found`

```bash
# Ubuntu/Debian
sudo apt-get install libreadline-dev

# RHEL/CentOS
sudo yum install readline-devel

# macOS
brew install readline
```

**Problem:** `flex: command not found`

```bash
# Ubuntu/Debian
sudo apt-get install flex bison

# RHEL/CentOS
sudo yum install flex bison

# macOS
brew install flex bison
```

### Initialization Failures

**Problem:** `initdb: could not find suitable text search configuration`

```bash
# Set locale explicitly
initdb -D $PGDATA --locale=en_US.UTF-8
```

**Problem:** Permission denied on data directory

```bash
# Fix permissions
chmod 700 $PGDATA
chown -R $(whoami) $PGDATA
```

### Runtime Issues

**Problem:** `psql: error: connection to server on socket "/tmp/.s.PGSQL.5432" failed`

```bash
# Check if PostgreSQL is running
pg_ctl -D $PGDATA status

# If not running, start it
pg_ctl -D $PGDATA -l logfile start

# Check log file for errors
cat $PGDATA/log/postgresql-*.log | tail -50
```

**Problem:** `FATAL: role "youruser" does not exist`

```bash
# Create your user role
createuser -s youruser

# Or within psql as superuser
psql -U postgres -c "CREATE ROLE youruser WITH LOGIN SUPERUSER;"
```

---

## Upgrade from Standard PostgreSQL

If you have an existing PostgreSQL installation without blockchain support:

### Option 1: Parallel Installation (Recommended)

Install to a different prefix to run both side-by-side:

```bash
./configure --prefix=/opt/pgsql-blockchain
make -j4
sudo make install

# Use different port for blockchain PostgreSQL
initdb -D /opt/pgsql-blockchain/data
echo "port = 5433" >> /opt/pgsql-blockchain/data/postgresql.conf
/opt/pgsql-blockchain/bin/pg_ctl -D /opt/pgsql-blockchain/data start
```

### Option 2: Migrate Existing Data

1. **Dump existing databases:**

    ```bash
    pg_dumpall -U postgres > all_databases.sql
    ```

2. **Stop old PostgreSQL:**

    ```bash
    pg_ctl -D /old/pgdata stop
    ```

3. **Install blockchain PostgreSQL** (follow steps above)

4. **Restore data:**

    ```bash
    psql -U postgres -f all_databases.sql
    ```

5. **Convert regular tables to blockchain tables:**

    ```sql
    -- Note: You cannot alter existing tables to blockchain tables
    -- You must create new blockchain tables and copy data

    CREATE BLOCKCHAIN TABLE new_audit_log (LIKE old_audit_log INCLUDING ALL);
    INSERT INTO new_audit_log SELECT * FROM old_audit_log;

    -- Rename tables
    ALTER TABLE old_audit_log RENAME TO old_audit_log_backup;
    ALTER TABLE new_audit_log RENAME TO audit_log;
    ```

---

## Next Steps

- [:octicons-arrow-right-24: Create Your First Blockchain Table](first-table.md)
- [:octicons-arrow-right-24: Quick Start Tutorial](quick-start.md)
- [:octicons-arrow-right-24: Configuration Guide](../deployment/configuration.md)
- [:octicons-arrow-right-24: User Guide](../user-guide/index.md)

---

## Additional Resources

- [PostgreSQL Official Documentation](https://www.postgresql.org/docs/)
- [Build PostgreSQL from Source](https://www.postgresql.org/docs/current/installation.html)
- [Deployment Best Practices](../deployment/index.md)
- [Troubleshooting Guide](../deployment/troubleshooting.md)
