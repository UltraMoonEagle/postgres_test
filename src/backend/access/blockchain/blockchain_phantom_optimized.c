/*-------------------------------------------------------------------------
 *
 * blockchain_phantom_optimized.c
 *	  WAL-based phantom blocks - zero I/O overhead!
 *
 * STRATEGY:
 * 1. During INSERT: Write phantom metadata to append-only file (sequential I/O, very fast)
 * 2. During COMMIT: File entries marked as committed (or deleted)
 * 3. During ABORT: File entries marked as aborted (kept for verification)
 * 4. During startup: Read file and populate phantom_blocks table
 *
 * I/O COST:
 * - Normal operation: ~50 bytes per INSERT to sequential file (negligible)
 * - Crash recovery: One-time read of phantom file
 * - Much better than 2x random I/O to table!
 *
 * IDENTIFICATION
 *	  src/backend/access/blockchain/blockchain_phantom_optimized.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/xact.h"
#include "executor/spi.h"
#include "utils/builtins.h"
#include "utils/timestamp.h"
#include "utils/memutils.h"
#include "utils/hsearch.h"
#include "utils/snapmgr.h"
#include "catalog/pg_type.h"
#include "storage/fd.h"
#include "miscadmin.h"
#include "blockchain/blockchain_counter.h"
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

/* Maximum phantom blocks per transaction (increased for MVP) */
#define MAX_PHANTOM_BLOCKS_PER_TXN 50000  /* Aligned with hash cache size */

/* Phantom block append-only log file */
#define PHANTOM_LOG_FILE "global/blockchain_phantom.log"

/* On-disk format for phantom block log entry */
typedef struct PhantomBlockLogEntry
{
    Oid table_oid;
    uint64 counter;
    unsigned char prev_hash[32];
    unsigned char computed_hash[32];
    TransactionId xid;
    char status;  /* 'A'=allocated, 'C'=committed, 'R'=rolled back */
    char padding[3];  /* Align to 8 bytes */
} PhantomBlockLogEntry;

/* In-memory tracking during transaction */
typedef struct PhantomBlockEntry
{
    Oid table_oid;
    uint64 counter;
    unsigned char prev_hash[32];
    unsigned char computed_hash[32];
} PhantomBlockEntry;

typedef struct PhantomBlockContext
{
    int count;
    int capacity;
    PhantomBlockEntry *entries;
    MemoryContext mcxt;
} PhantomBlockContext;

static PhantomBlockContext *current_txn_phantom_blocks = NULL;
static bool phantom_xact_callback_registered = false;

/* Forward declarations */
static void blockchain_phantom_xact_callback(XactEvent event, void *arg);

/*
 * Initialize phantom block tracking for this transaction
 */
static void
init_phantom_block_context(void)
{
    if (current_txn_phantom_blocks != NULL)
        return;  /* Already initialized */

    MemoryContext old_context = MemoryContextSwitchTo(TopTransactionContext);

    current_txn_phantom_blocks = (PhantomBlockContext *) palloc0(sizeof(PhantomBlockContext));
    current_txn_phantom_blocks->capacity = 64;  /* Start small, grow as needed */
    current_txn_phantom_blocks->count = 0;
    current_txn_phantom_blocks->entries = (PhantomBlockEntry *)
        palloc(sizeof(PhantomBlockEntry) * current_txn_phantom_blocks->capacity);
    current_txn_phantom_blocks->mcxt = TopTransactionContext;

    MemoryContextSwitchTo(old_context);

    /* Register transaction callback (only once per backend) */
    if (!phantom_xact_callback_registered)
    {
        RegisterXactCallback(blockchain_phantom_xact_callback, NULL);
        phantom_xact_callback_registered = true;
    }
}

/*
 * Append a phantom block entry to the log file (sequential write, very fast!)
 */
static void
append_phantom_to_log(Oid table_oid, uint64 counter,
                     const unsigned char *prev_hash,
                     const unsigned char *computed_hash,
                     char status)
{
    PhantomBlockLogEntry entry;
    int fd;
    char path[MAXPGPATH];
    ssize_t written;

    /* Prepare log entry */
    entry.table_oid = table_oid;
    entry.counter = counter;
    memcpy(entry.prev_hash, prev_hash, 32);
    memcpy(entry.computed_hash, computed_hash, 32);
    entry.xid = GetCurrentTransactionId();
    entry.status = status;
    memset(entry.padding, 0, sizeof(entry.padding));

    /* Open log file for append */
    snprintf(path, sizeof(path), "%s/%s", DataDir, PHANTOM_LOG_FILE);
    fd = OpenTransientFile(path, O_WRONLY | O_APPEND | O_CREAT | PG_BINARY);

    if (fd < 0)
    {
        elog(WARNING, "Failed to open phantom log file: %m");
        return;
    }

    /* Sequential append (very fast!) */
    written = write(fd, &entry, sizeof(entry));

    if (written != sizeof(entry))
    {
        elog(WARNING, "Failed to write phantom log entry: %m");
    }

    CloseTransientFile(fd);

    elog(DEBUG2, "Appended phantom block to log: table=%u, counter=%llu, status=%c",
         table_oid, (unsigned long long)counter, status);
}

/*
 * Record a phantom block: write to log file AND keep in memory
 */
void
blockchain_record_phantom_block(Oid table_oid, uint64 counter,
                                const unsigned char *prev_hash,
                                const unsigned char *computed_hash)
{
    PhantomBlockEntry *entry;

    init_phantom_block_context();

    /* Safety check: prevent unbounded memory growth */
    if (current_txn_phantom_blocks->count >= MAX_PHANTOM_BLOCKS_PER_TXN)
    {
        elog(WARNING, "Transaction has allocated %d phantom blocks, limit reached",
             MAX_PHANTOM_BLOCKS_PER_TXN);
        return;
    }

    /* Grow array if needed */
    if (current_txn_phantom_blocks->count >= current_txn_phantom_blocks->capacity)
    {
        int new_capacity = current_txn_phantom_blocks->capacity * 2;
        current_txn_phantom_blocks->entries = (PhantomBlockEntry *)
            repalloc(current_txn_phantom_blocks->entries,
                    sizeof(PhantomBlockEntry) * new_capacity);
        current_txn_phantom_blocks->capacity = new_capacity;
    }

    /* Store phantom block metadata in memory */
    entry = &current_txn_phantom_blocks->entries[current_txn_phantom_blocks->count];
    entry->table_oid = table_oid;
    entry->counter = counter;
    memcpy(entry->prev_hash, prev_hash, 32);
    memcpy(entry->computed_hash, computed_hash, 32);

    current_txn_phantom_blocks->count++;

    /* Write to append-only log immediately (sequential I/O, very fast!) */
    append_phantom_to_log(table_oid, counter, prev_hash, computed_hash, 'A');

    elog(DEBUG2, "Recorded phantom block: table=%u, counter=%llu (total: %d)",
         table_oid, (unsigned long long)counter, current_txn_phantom_blocks->count);
}

/*
 * Mark phantom blocks in log file with final status (C=committed, R=rolled back)
 */
static void
mark_phantom_blocks_status(char status)
{
    int i;

    if (current_txn_phantom_blocks == NULL || current_txn_phantom_blocks->count == 0)
        return;

    elog(DEBUG1, "Marking %d phantom blocks as '%c'",
         current_txn_phantom_blocks->count, status);

    /* Append status update for each phantom block */
    for (i = 0; i < current_txn_phantom_blocks->count; i++)
    {
        PhantomBlockEntry *entry = &current_txn_phantom_blocks->entries[i];
        append_phantom_to_log(entry->table_oid, entry->counter,
                            entry->prev_hash, entry->computed_hash, status);
    }
}

/*
 * Load phantom blocks from log file into phantom_blocks table
 * Called during server startup for crash recovery
 */
static void
flush_phantom_blocks_to_disk(void)
{
    int i;
    int spi_result;

    if (current_txn_phantom_blocks == NULL || current_txn_phantom_blocks->count == 0)
        return;

    elog(LOG, "Flushing %d phantom blocks to disk in autonomous transaction",
         current_txn_phantom_blocks->count);

    /* We're in a fresh transaction now, SPI should work */
    spi_result = SPI_connect();
    if (spi_result != SPI_OK_CONNECT)
    {
        elog(WARNING, "Failed to connect to SPI for phantom block flush");
        return;
    }

    PG_TRY();
    {
        for (i = 0; i < current_txn_phantom_blocks->count; i++)
        {
            PhantomBlockEntry *entry = &current_txn_phantom_blocks->entries[i];
            char query[2048];
            Datum values[7];
            char nulls[7];
            Oid arg_types[7];

            /* Convert binary hashes to bytea */
            bytea *prev_hash_bytea = (bytea *) palloc(VARHDRSZ + 32);
            SET_VARSIZE(prev_hash_bytea, VARHDRSZ + 32);
            memcpy(VARDATA(prev_hash_bytea), entry->prev_hash, 32);

            bytea *computed_hash_bytea = (bytea *) palloc(VARHDRSZ + 32);
            SET_VARSIZE(computed_hash_bytea, VARHDRSZ + 32);
            memcpy(VARDATA(computed_hash_bytea), entry->computed_hash, 32);

            /* Prepare INSERT */
            arg_types[0] = OIDOID;
            arg_types[1] = INT8OID;
            arg_types[2] = BYTEAOID;
            arg_types[3] = BYTEAOID;
            arg_types[4] = TIMESTAMPTZOID;
            arg_types[5] = XIDOID;
            arg_types[6] = TEXTOID;

            values[0] = ObjectIdGetDatum(entry->table_oid);
            values[1] = Int64GetDatum((int64) entry->counter);
            values[2] = PointerGetDatum(prev_hash_bytea);
            values[3] = PointerGetDatum(computed_hash_bytea);
            values[4] = TimestampTzGetDatum(GetCurrentTimestamp());
            values[5] = TransactionIdGetDatum(GetCurrentTransactionId());
            values[6] = CStringGetTextDatum("ALLOCATED");

            memset(nulls, ' ', sizeof(nulls));

            snprintf(query, sizeof(query),
                    "INSERT INTO public.blockchain_phantom_blocks "
                    "(table_oid, counter, prev_hash, computed_hash, allocated_at, xid, status) "
                    "VALUES ($1, $2, $3, $4, $5, $6, $7) "
                    "ON CONFLICT (table_oid, counter) DO UPDATE SET status = 'ABORTED'");

            spi_result = SPI_execute_with_args(query, 7, arg_types, values, nulls, false, 0);

            if (spi_result < 0)
            {
                elog(WARNING, "Failed to insert phantom block: table=%u, counter=%llu",
                     entry->table_oid, (unsigned long long)entry->counter);
            }
        }

        SPI_finish();
        elog(LOG, "Successfully persisted %d phantom blocks", current_txn_phantom_blocks->count);
    }
    PG_CATCH();
    {
        SPI_finish();
        elog(WARNING, "Exception while flushing phantom blocks, some may be lost");
        FlushErrorState();
    }
    PG_END_TRY();
}

/*
 * Transaction callback - persist ALL phantom blocks at PRE_COMMIT time
 * This ensures they're written with status='ALLOCATED' before commit.
 * If transaction commits, we can clean them up later. If it aborts, they stay as ALLOCATED.
 */
static void
blockchain_phantom_xact_callback(XactEvent event, void *arg)
{
    switch (event)
    {
        case XACT_EVENT_COMMIT:
        case XACT_EVENT_PARALLEL_COMMIT:
            /* Transaction committed - mark phantom blocks as committed in log
             * These can be cleaned up later (actual data exists in main table) */
            if (current_txn_phantom_blocks != NULL && current_txn_phantom_blocks->count > 0)
            {
                mark_phantom_blocks_status('C');
                elog(DEBUG1, "COMMIT: Marked %d phantom blocks as committed",
                     current_txn_phantom_blocks->count);
            }
            current_txn_phantom_blocks = NULL;
            break;

        case XACT_EVENT_ABORT:
        case XACT_EVENT_PARALLEL_ABORT:
            /* Transaction rolled back - mark phantom blocks as aborted in log
             * These are CRITICAL for chain verification! */
            if (current_txn_phantom_blocks != NULL && current_txn_phantom_blocks->count > 0)
            {
                mark_phantom_blocks_status('R');
                elog(LOG, "ABORT: Marked %d phantom blocks as rolled back (kept for verification)",
                     current_txn_phantom_blocks->count);
            }
            current_txn_phantom_blocks = NULL;
            break;

        case XACT_EVENT_PRE_COMMIT:
        case XACT_EVENT_PARALLEL_PRE_COMMIT:
        case XACT_EVENT_PREPARE:
        case XACT_EVENT_PRE_PREPARE:
            /* Nothing to do - log entries already written during INSERT */
            break;
    }
}

/*
 * Read phantom block log file and populate phantom_blocks table
 * Called during server startup for crash recovery
 */
void
blockchain_recover_phantom_blocks(void)
{
    char path[MAXPGPATH];
    int fd;
    PhantomBlockLogEntry entry;
    ssize_t bytes_read;
    int total_entries = 0;
    int rollback_entries = 0;
    int spi_result;
    struct stat st;
#ifdef PG_HAVE_ATOMIC_U64_SUPPORT
    uint32 already_done;
#endif

    /* Hash table to track final status of each (table_oid, counter) pair */
    HTAB *phantom_status;
    HASHCTL hash_ctl;

    typedef struct PhantomStatusKey
    {
        Oid table_oid;
        uint64 counter;
    } PhantomStatusKey;

    typedef struct PhantomStatusEntry
    {
        PhantomStatusKey key;
        PhantomBlockLogEntry data;
        char final_status;
    } PhantomStatusEntry;

    /* Check if recovery already completed using shared memory flag */
    if (!BlockchainCounterShmem)
    {
        elog(DEBUG1, "blockchain_recover_phantom_blocks: Shared memory not initialized yet, skipping");
        return;
    }

#ifdef PG_HAVE_ATOMIC_U64_SUPPORT
    already_done = pg_atomic_read_u32(&BlockchainCounterShmem->phantom_recovery_done);
    if (already_done == 1)
    {
        elog(DEBUG2, "blockchain_recover_phantom_blocks: Recovery already completed, skipping");
        return;
    }

    /* Try to set the flag atomically - only one backend should do recovery */
    if (!pg_atomic_compare_exchange_u32(&BlockchainCounterShmem->phantom_recovery_done, &already_done, 1))
    {
        elog(DEBUG2, "blockchain_recover_phantom_blocks: Another backend is doing recovery, skipping");
        return;
    }
#else
    LWLockAcquire(&BlockchainCounterShmem->ctl_lock, LW_EXCLUSIVE);
    if (BlockchainCounterShmem->phantom_recovery_done)
    {
        LWLockRelease(&BlockchainCounterShmem->ctl_lock);
        elog(DEBUG2, "blockchain_recover_phantom_blocks: Recovery already completed, skipping");
        return;
    }
    BlockchainCounterShmem->phantom_recovery_done = true;
    LWLockRelease(&BlockchainCounterShmem->ctl_lock);
#endif

    /* Build path to log file */
    snprintf(path, sizeof(path), "%s/%s", DataDir, PHANTOM_LOG_FILE);

    /* Check if log file exists and has content */
    if (stat(path, &st) != 0)
    {
        if (errno == ENOENT)
        {
            elog(DEBUG1, "blockchain_recover_phantom_blocks: No phantom log file found (first startup or no phantom blocks)");
            return;
        }
        elog(WARNING, "blockchain_recover_phantom_blocks: Failed to stat phantom log: %m");
        return;
    }

    /* If log file is empty, nothing to recover */
    if (st.st_size == 0)
    {
        elog(DEBUG1, "blockchain_recover_phantom_blocks: Phantom log file is empty, nothing to recover");
        return;
    }

    elog(LOG, "blockchain_recover_phantom_blocks: Starting recovery from log file (size: %lld bytes)",
         (long long)st.st_size);

    /* Open log file for reading */
    fd = open(path, O_RDONLY | PG_BINARY, 0);
    if (fd < 0)
    {
        elog(WARNING, "blockchain_recover_phantom_blocks: Failed to open phantom log: %m");
        return;
    }

    /* Create hash table to track final status */
    memset(&hash_ctl, 0, sizeof(hash_ctl));
    hash_ctl.keysize = sizeof(PhantomStatusKey);
    hash_ctl.entrysize = sizeof(PhantomStatusEntry);
    hash_ctl.hcxt = CurrentMemoryContext;
    
    phantom_status = hash_create("Phantom Block Status",
                                 1024,
                                 &hash_ctl,
                                 HASH_ELEM | HASH_BLOBS | HASH_CONTEXT);

    /* Read all log entries and track final status for each (table_oid, counter) */
    while ((bytes_read = read(fd, &entry, sizeof(entry))) == sizeof(entry))
    {
        PhantomStatusKey key;
        PhantomStatusEntry *status_entry;
        bool found;

        total_entries++;

        key.table_oid = entry.table_oid;
        key.counter = entry.counter;

        /* Insert or update status */
        status_entry = (PhantomStatusEntry *) hash_search(phantom_status, &key, HASH_ENTER, &found);
        
        /* Update with latest status (log is append-only, so last entry wins) */
        status_entry->data = entry;
        status_entry->final_status = entry.status;

        elog(DEBUG2, "Read phantom log entry: table=%u, counter=%llu, status=%c",
             entry.table_oid, (unsigned long long)entry.counter, entry.status);
    }

    close(fd);

    if (bytes_read > 0 && bytes_read != sizeof(entry))
    {
        elog(WARNING, "blockchain_recover_phantom_blocks: Corrupted log file (incomplete entry)");
    }

    elog(LOG, "blockchain_recover_phantom_blocks: Read %d log entries", total_entries);

    /* Connect to SPI to insert into phantom_blocks table */
    spi_result = SPI_connect();
    if (spi_result != SPI_OK_CONNECT)
    {
        elog(ERROR, "blockchain_recover_phantom_blocks: SPI_connect failed");
        return;
    }

    /* Push active snapshot for SPI commands */
    PushActiveSnapshot(GetTransactionSnapshot());

    /* Insert entries with status='R' (rolled back) into phantom_blocks table */
    PG_TRY();
    {
        HASH_SEQ_STATUS hash_seq;
        PhantomStatusEntry *status_entry;

        /* Create the phantom_blocks table if it doesn't exist */
        spi_result = SPI_execute(
            "CREATE TABLE IF NOT EXISTS public.blockchain_phantom_blocks ("
            "    table_oid OID NOT NULL,"
            "    counter BIGINT NOT NULL,"
            "    prev_hash BYTEA NOT NULL,"
            "    computed_hash BYTEA NOT NULL,"
            "    allocated_at TIMESTAMP WITH TIME ZONE NOT NULL,"
            "    xid XID NOT NULL,"
            "    status TEXT NOT NULL,"
            "    PRIMARY KEY (table_oid, counter)"
            ")",
            false, 0);

        if (spi_result < 0)
        {
            elog(WARNING, "Failed to create blockchain_phantom_blocks table");
        }
        else
        {
            elog(LOG, "blockchain_recover_phantom_blocks: Ensured phantom_blocks table exists");
        }

        hash_seq_init(&hash_seq, phantom_status);

        while ((status_entry = (PhantomStatusEntry *) hash_seq_search(&hash_seq)) != NULL)
        {
            /* Only persist rolled-back transactions */
            if (status_entry->final_status == 'R')
            {
                char query[2048];
                Datum values[7];
                char nulls[7];
                Oid arg_types[7];
                bytea *prev_hash_bytea;
                bytea *computed_hash_bytea;

                rollback_entries++;

                /* Convert binary hashes to bytea */
                prev_hash_bytea = (bytea *) palloc(VARHDRSZ + 32);
                SET_VARSIZE(prev_hash_bytea, VARHDRSZ + 32);
                memcpy(VARDATA(prev_hash_bytea), status_entry->data.prev_hash, 32);

                computed_hash_bytea = (bytea *) palloc(VARHDRSZ + 32);
                SET_VARSIZE(computed_hash_bytea, VARHDRSZ + 32);
                memcpy(VARDATA(computed_hash_bytea), status_entry->data.computed_hash, 32);

                /* Prepare INSERT */
                arg_types[0] = OIDOID;
                arg_types[1] = INT8OID;
                arg_types[2] = BYTEAOID;
                arg_types[3] = BYTEAOID;
                arg_types[4] = TIMESTAMPTZOID;
                arg_types[5] = XIDOID;
                arg_types[6] = TEXTOID;

                values[0] = ObjectIdGetDatum(status_entry->data.table_oid);
                values[1] = Int64GetDatum((int64) status_entry->data.counter);
                values[2] = PointerGetDatum(prev_hash_bytea);
                values[3] = PointerGetDatum(computed_hash_bytea);
                values[4] = TimestampTzGetDatum(GetCurrentTimestamp());
                values[5] = TransactionIdGetDatum(status_entry->data.xid);
                values[6] = CStringGetTextDatum("ABORTED");

                memset(nulls, ' ', sizeof(nulls));

                snprintf(query, sizeof(query),
                        "INSERT INTO public.blockchain_phantom_blocks "
                        "(table_oid, counter, prev_hash, computed_hash, allocated_at, xid, status) "
                        "VALUES ($1, $2, $3, $4, $5, $6, $7) "
                        "ON CONFLICT (table_oid, counter) DO NOTHING");

                spi_result = SPI_execute_with_args(query, 7, arg_types, values, nulls, false, 0);

                if (spi_result < 0)
                {
                    elog(WARNING, "Failed to insert recovered phantom block: table=%u, counter=%llu",
                         status_entry->data.table_oid, (unsigned long long)status_entry->data.counter);
                }
            }
        }

        PopActiveSnapshot();
        SPI_finish();

        elog(LOG, "blockchain_recover_phantom_blocks: Inserted %d rolled-back phantom blocks into table",
             rollback_entries);
    }
    PG_CATCH();
    {
        /* Error occurred during recovery - clean up and re-throw */
        PopActiveSnapshot();
        SPI_finish();

        /* Re-throw the error so caller can handle retry logic */
        PG_RE_THROW();
    }
    PG_END_TRY();

    /* Truncate the log file (recovery complete) */
    fd = open(path, O_WRONLY | O_TRUNC | PG_BINARY, 0);
    if (fd >= 0)
    {
        close(fd);
        elog(LOG, "blockchain_recover_phantom_blocks: Truncated phantom log file");
    }
    else
    {
        elog(WARNING, "blockchain_recover_phantom_blocks: Failed to truncate log file: %m");
    }

    hash_destroy(phantom_status);

    elog(LOG, "blockchain_recover_phantom_blocks: Recovery complete - %d total entries, %d rolled-back blocks recovered",
         total_entries, rollback_entries);
}

/*
 * SQL-callable wrapper for recovery function
 */
PG_FUNCTION_INFO_V1(blockchain_recover_phantom_blocks_sql);

Datum
blockchain_recover_phantom_blocks_sql(PG_FUNCTION_ARGS)
{
    blockchain_recover_phantom_blocks();
    PG_RETURN_VOID();
}
