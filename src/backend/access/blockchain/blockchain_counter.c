/*-------------------------------------------------------------------------
 *
 * blockchain_counter.c
 *	  Global counter infrastructure for blockchain tables
 *
 * This module implements a shared memory-based counter system that provides
 * monotonically increasing counter values for blockchain tables. This
 * replaces the LSN-based approach and eliminates double inserts.
 *
 * PHASE 1 ENHANCEMENTS:
 * - Lock-free atomic counters using pg_atomic_uint64
 * - Lazy recovery with atomic initialization flags
 * - Fallback to LWLock-based implementation when atomics unavailable
 *
 * Portions Copyright (c) 1996-2025, PostgreSQL Global Development Group
 *
 * src/backend/access/blockchain/blockchain_counter.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "blockchain/blockchain_counter.h"
#include "miscadmin.h"
#include "port/atomics.h"
#include "storage/ipc.h"
#include "storage/lmgr.h"
#include "storage/lwlock.h"
#include "storage/shmem.h"
#include "utils/builtins.h"
#include "utils/dynahash.h"
#include "utils/memutils.h"
#include "access/xlog.h"
#include "catalog/pg_class.h"
#include "access/htup_details.h"
#include "access/table.h"
#include "utils/rel.h"
#include "utils/syscache.h"
#include "storage/fd.h"
#include "pgstat.h"
#include "common/hashfn.h"
#include "executor/spi.h"
#include "catalog/namespace.h"
#include "utils/builtins.h"
#include "catalog/pg_class.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "access/xact.h"

/* Global pointer to shared memory data */
BlockchainCounterData *BlockchainCounterShmem = NULL;

/* GUC parameters - defined here, declared in header */
bool blockchain_use_atomic_counters = true;
int blockchain_hash_cache_partitions = 64;
int blockchain_retry_initial_us = 100;
int blockchain_retry_max_us = 10000;
int blockchain_retry_max_total_ms = 1000;
int blockchain_batch_max_size = 256;

/* Transaction rollback tracking */
typedef struct BlockchainCounterReservation
{
    Oid table_oid;
    uint64 reserved_counter;
    uint64 previous_counter;  /* Counter value before this reservation */
    bool committed;
    struct BlockchainCounterReservation *next;
} BlockchainCounterReservation;

/* Per-backend transaction reservation tracking - this should be per-process */
static BlockchainCounterReservation *current_reservations = NULL;

/* Forward declarations */
static uint64 BlockchainRecoverCounterForTable(Oid table_oid);
static void blockchain_xact_callback(XactEvent event, void *arg);
static void blockchain_subxact_callback(SubXactEvent event, SubTransactionId mySubid,
                                       SubTransactionId parentSubid, void *arg);

/* File to persist counter values */
#define BLOCKCHAIN_COUNTER_FILE "global/blockchain_counters"

/*
 * blockchain_counter_shmem_exit - shutdown callback for shared memory exit
 */
static void
blockchain_counter_shmem_exit(int code, Datum arg)
{
	BlockchainPersistCounters();
}

/*
 * BlockchainCounterShmemSize
 *		Compute space needed for blockchain counter shared memory
 */
Size
BlockchainCounterShmemSize(void)
{
	Size		size;
	Size		counter_hash_size;
	Size		hash_cache_size;

	size = MAXALIGN(sizeof(BlockchainCounterData));
	counter_hash_size = hash_estimate_size(MAX_BLOCKCHAIN_TABLES,
										   sizeof(BlockchainCounterEntry));
	hash_cache_size = hash_estimate_size(MAX_CACHED_HASHES,
										 sizeof(BlockchainHashEntry));

	size = add_size(size, counter_hash_size);
	size = add_size(size, hash_cache_size);

	elog(LOG, "BlockchainCounterShmemSize: total=%zu bytes (%.2f MB), "
		 "counter_hash=%zu bytes (%.2f MB), hash_cache=%zu bytes (%.2f MB), "
		 "MAX_CACHED_HASHES=%d",
		 size, size / (1024.0 * 1024.0),
		 counter_hash_size, counter_hash_size / (1024.0 * 1024.0),
		 hash_cache_size, hash_cache_size / (1024.0 * 1024.0),
		 MAX_CACHED_HASHES);

	return size;
}

/*
 * BlockchainCounterShmemInit
 *		Initialize blockchain counter shared memory structures
 */
void
BlockchainCounterShmemInit(void)
{
	bool		found;
	HASHCTL		hash_ctl;

	/* Create or attach to the shared memory region */
	BlockchainCounterShmem = (BlockchainCounterData *)
		ShmemInitStruct("Blockchain Counter Data",
						sizeof(BlockchainCounterData),
						&found);

	if (!found)
	{
		/* First time through, so initialize */
		LWLockInitialize(&BlockchainCounterShmem->ctl_lock,
						 LWTRANCHE_FIRST_USER_DEFINED);
		LWLockInitialize(&BlockchainCounterShmem->hash_cache_lock,
						 LWTRANCHE_FIRST_USER_DEFINED + 2);

		/* Initialize phantom recovery flag */
#ifdef PG_HAVE_ATOMIC_U64_SUPPORT
		pg_atomic_init_u32(&BlockchainCounterShmem->phantom_recovery_done, 0);
#else
		BlockchainCounterShmem->phantom_recovery_done = false;
#endif

		/* Create hash table for counter entries */
		MemSet(&hash_ctl, 0, sizeof(hash_ctl));
		hash_ctl.keysize = sizeof(Oid);
		hash_ctl.entrysize = sizeof(BlockchainCounterEntry);
		hash_ctl.hash = uint32_hash;

		BlockchainCounterShmem->counter_hash =
			ShmemInitHash("Blockchain Counter Hash",
						  MAX_BLOCKCHAIN_TABLES,
						  MAX_BLOCKCHAIN_TABLES,
						  &hash_ctl,
						  HASH_ELEM | HASH_FUNCTION);

		/* Create hash table for uncommitted hash cache */
		MemSet(&hash_ctl, 0, sizeof(hash_ctl));
		hash_ctl.keysize = sizeof(BlockchainHashKey);
		hash_ctl.entrysize = sizeof(BlockchainHashEntry);

		BlockchainCounterShmem->hash_cache =
			ShmemInitHash("Blockchain Hash Cache",
						  MAX_CACHED_HASHES,
						  MAX_CACHED_HASHES,
						  &hash_ctl,
						  HASH_ELEM | HASH_BLOBS);
	}
}

/*
 * BlockchainGetNextCounter
 *		Get the next counter value for a blockchain table
 *
 * PHASE 1: Uses atomic operations when enabled via GUC, falls back to LWLock.
 * Implements lazy recovery with atomic initialization flags.
 */
uint64
BlockchainGetNextCounter(Oid table_oid)
{
	BlockchainCounterEntry *entry;
	bool		found;
	uint64		result;

	elog(DEBUG1, "DEBUG: BlockchainGetNextCounter - START - table_oid=%u, PID=%d", table_oid, MyProcPid);

	if (!BlockchainCounterShmem)
		elog(ERROR, "blockchain counter shared memory not initialized");

	elog(DEBUG1, "DEBUG: BlockchainGetNextCounter - shmem OK, acquiring lock, PID=%d", MyProcPid);

#ifdef PG_HAVE_ATOMIC_U64_SUPPORT
	if (blockchain_use_atomic_counters)
	{
		/* FAST PATH: Lock-free atomic counters with lazy initialization */

		/* First, try to find existing entry with shared lock */
		LWLockAcquire(&BlockchainCounterShmem->ctl_lock, LW_SHARED);
		entry = (BlockchainCounterEntry *) hash_search(BlockchainCounterShmem->counter_hash,
														&table_oid,
														HASH_FIND,
														&found);
		LWLockRelease(&BlockchainCounterShmem->ctl_lock);

		if (!found)
		{
			/* Need to create entry - acquire exclusive lock */
			LWLockAcquire(&BlockchainCounterShmem->ctl_lock, LW_EXCLUSIVE);
			entry = (BlockchainCounterEntry *) hash_search(BlockchainCounterShmem->counter_hash,
															&table_oid,
															HASH_ENTER,
															&found);

			if (!found)
			{
				/* We created the entry - initialize atomic fields */
				entry->table_oid = table_oid;
				pg_atomic_init_u64(&entry->counter_value_atomic, 0);
				pg_atomic_init_u32(&entry->initialized, 0);
				entry->counter_value = 0; /* shadow value for persistence */
				entry->last_persisted = 0;
				LWLockInitialize(&entry->lock, LWTRANCHE_FIRST_USER_DEFINED + 1);

				elog(LOG, "Created new atomic counter entry for table OID %u", table_oid);
			}
			LWLockRelease(&BlockchainCounterShmem->ctl_lock);
		}

		/* Check if lazy recovery is needed (atomic read) */
		if (pg_atomic_read_u32(&entry->initialized) == 0)
		{
			/* Lazy recovery: acquire entry lock to prevent concurrent recovery */
			LWLockAcquire(&entry->lock, LW_EXCLUSIVE);

			/* Double-check after acquiring lock */
			if (pg_atomic_read_u32(&entry->initialized) == 0)
			{
				uint64 recovered_max = BlockchainRecoverCounterForTable(table_oid);

				/* Initialize atomic counter to recovered_max + 1 */
				pg_atomic_write_u64(&entry->counter_value_atomic, recovered_max + 1);
				entry->counter_value = recovered_max + 1; /* sync shadow */

				/* Mark as initialized (atomic write with memory barrier) */
				pg_atomic_write_u32(&entry->initialized, 1);

				elog(LOG, "Atomic counter lazy recovery for table OID %u: starting at %llu",
					 table_oid, (unsigned long long)(recovered_max + 1));
			}

			LWLockRelease(&entry->lock);
		}

		/* Atomic fetch-and-add (lock-free!) */
		result = pg_atomic_fetch_add_u64(&entry->counter_value_atomic, 1);

		elog(LOG, "BLOCKCHAIN ATOMIC COUNTER: table_oid=%u, counter=%llu, PID=%d",
			 table_oid, (unsigned long long)result, MyProcPid);

		return result;
	}
#endif

	/* FALLBACK PATH: LWLock-based implementation */
	elog(DEBUG1, "Using LWLock-based counter (atomics disabled or unavailable)");

	/* Look up or create counter entry */
	LWLockAcquire(&BlockchainCounterShmem->ctl_lock, LW_EXCLUSIVE);

	elog(DEBUG1, "DEBUG: BlockchainGetNextCounter - lock acquired, doing hash search, PID=%d", MyProcPid);

	entry = (BlockchainCounterEntry *) hash_search(BlockchainCounterShmem->counter_hash,
													&table_oid,
													HASH_ENTER,
													&found);

	elog(LOG, "HASH SEARCH: table_oid=%u, found=%s, entry=%p, counter_value=%llu, PID=%d",
	     table_oid, found ? "true" : "false", entry,
	     entry ? (unsigned long long)entry->counter_value : 0, MyProcPid);

	if (!found)
	{
		/* New table - perform lazy recovery to find existing maximum counter */
		uint64 recovered_max = BlockchainRecoverCounterForTable(table_oid);

		/* Initialize new counter entry */
		entry->table_oid = table_oid;
		entry->counter_value = recovered_max + 1;  /* Next counter after maximum found */
		entry->last_persisted = 0;
		LWLockInitialize(&entry->lock, LWTRANCHE_FIRST_USER_DEFINED + 1);

		elog(LOG, "Lazy recovery for table OID %u: starting counter at %llu",
			 table_oid, (unsigned long long)entry->counter_value);
	}

	/* Release control lock before acquiring entry lock (original ordering) */
	LWLockRelease(&BlockchainCounterShmem->ctl_lock);

	elog(DEBUG1, "DEBUG: BlockchainGetNextCounter - control lock released, acquiring entry lock, PID=%d", MyProcPid);

	/* Get counter value but only increment if in a transaction */
	LWLockAcquire(&entry->lock, LW_EXCLUSIVE);

	elog(DEBUG1, "DEBUG: BlockchainGetNextCounter - entry lock acquired, entry=%p, counter_value=%llu, PID=%d",
	     entry, (unsigned long long)entry->counter_value, MyProcPid);

	/* BLOCKCHAIN IMMUTABILITY: Simple counter increment with NO rollback
	 *
	 * For blockchain systems, counter gaps from failed transactions are CORRECT behavior:
	 * 1. Maintains immutability - failed attempts should leave traces
	 * 2. Avoids dangerous race conditions in concurrent rollback scenarios
	 * 3. Follows precedent of Bitcoin/Ethereum nonce handling
	 * 4. Simpler, faster, and safer than complex reservation systems
	 */
	result = entry->counter_value++;

	elog(LOG, "BLOCKCHAIN COUNTER (LWLock): table_oid=%u, counter=%llu, PID=%d",
	     table_oid, (unsigned long long)result, MyProcPid);

	elog(DEBUG1, "DEBUG: BlockchainGetNextCounter - about to release entry lock and return %llu, PID=%d",
	     (unsigned long long)result, MyProcPid);

	LWLockRelease(&entry->lock);

	elog(DEBUG1, "DEBUG: BlockchainGetNextCounter - COMPLETED successfully, returning %llu, PID=%d",
	     (unsigned long long)result, MyProcPid);

	return result;
}

/*
 * BlockchainPersistCounters
 *		Write current counter values to disk
 *
 * This is called periodically and at shutdown to ensure counter
 * values survive restarts.
 */
void
BlockchainPersistCounters(void)
{
	HASH_SEQ_STATUS status;
	BlockchainCounterEntry *entry;
	File		file;
	char		tmppath[MAXPGPATH];
	char		path[MAXPGPATH];

	if (!BlockchainCounterShmem)
		return;

	/* Write to temporary file first */
	snprintf(tmppath, sizeof(tmppath), "%s.tmp", BLOCKCHAIN_COUNTER_FILE);
	snprintf(path, sizeof(path), "%s", BLOCKCHAIN_COUNTER_FILE);

	file = PathNameOpenFile(tmppath, O_WRONLY | O_CREAT | O_TRUNC);
	if (file < 0)
	{
		ereport(WARNING,
				(errcode_for_file_access(),
				 errmsg("could not create blockchain counter file \"%s\": %m",
						tmppath)));
		return;
	}

	/* Write all counter entries */
	LWLockAcquire(&BlockchainCounterShmem->ctl_lock, LW_SHARED);
	
	off_t offset;
	
	offset = 0;
	hash_seq_init(&status, BlockchainCounterShmem->counter_hash);
	while ((entry = (BlockchainCounterEntry *) hash_seq_search(&status)) != NULL)
	{
		uint64 counter_to_persist;

		/* Write table OID */
		if (FileWrite(file, &entry->table_oid, sizeof(Oid), offset, WAIT_EVENT_DATA_FILE_WRITE) != sizeof(Oid))
		{
			ereport(WARNING,
					(errcode_for_file_access(),
					 errmsg("could not write to blockchain counter file: %m")));
			hash_seq_term(&status);
			FileClose(file);
			LWLockRelease(&BlockchainCounterShmem->ctl_lock);
			return;
		}
		offset += sizeof(Oid);

		/* Get counter value (atomic-safe) */
#ifdef PG_HAVE_ATOMIC_U64_SUPPORT
		if (blockchain_use_atomic_counters && pg_atomic_read_u32(&entry->initialized) == 1)
			counter_to_persist = pg_atomic_read_u64(&entry->counter_value_atomic);
		else
#endif
			counter_to_persist = entry->counter_value;

		if (FileWrite(file, &counter_to_persist, sizeof(uint64), offset, WAIT_EVENT_DATA_FILE_WRITE) != sizeof(uint64))
		{
			ereport(WARNING,
					(errcode_for_file_access(),
					 errmsg("could not write to blockchain counter file: %m")));
			hash_seq_term(&status);
			FileClose(file);
			LWLockRelease(&BlockchainCounterShmem->ctl_lock);
			return;
		}
		offset += sizeof(uint64);
	}
	
	LWLockRelease(&BlockchainCounterShmem->ctl_lock);

	if (FileSync(file, WAIT_EVENT_DATA_FILE_SYNC) != 0)
	{
		ereport(WARNING,
				(errcode_for_file_access(),
				 errmsg("could not sync blockchain counter file: %m")));
	}

	FileClose(file);

	/* Rename temp file to final name */
	if (rename(tmppath, path) != 0)
	{
		ereport(WARNING,
				(errcode_for_file_access(),
				 errmsg("could not rename blockchain counter file: %m")));
	}
}

/*
 * BlockchainRecoverCounterForTable
 *	Recover counter value for a specific blockchain table using lazy loading
 */
static uint64
BlockchainRecoverCounterForTable(Oid table_oid)
{
	Relation	table_rel;
	uint64		max_counter = 0;
	
	if (!OidIsValid(table_oid))
		return 0;

	/* Try to open the table and check if it's a blockchain table */
	PG_TRY();
	{
		TupleDesc	tupdesc;
		bool		is_blockchain_table = false;
		
		table_rel = table_open(table_oid, AccessShareLock);
		tupdesc = RelationGetDescr(table_rel);
		
		/* Check if table has blockchain system columns */
		for (int i = 0; i < tupdesc->natts; i++)
		{
			Form_pg_attribute attr = TupleDescAttr(tupdesc, i);
			if (strcmp(NameStr(attr->attname), "__tx_lsn") == 0)
			{
				is_blockchain_table = true;
				break;
			}
		}
		
		if (is_blockchain_table)
		{
			/* Find maximum counter value for this table using SPI */
			char		query[512];
			int			spi_result;
			SPITupleTable *tuptable;
			bool		isnull;
			Datum		max_counter_datum;
			
			spi_result = SPI_connect();
			if (spi_result == SPI_OK_CONNECT)
			{
				snprintf(query, sizeof(query), 
					"SELECT COALESCE(MAX(__tx_lsn), 0) FROM %s",
					RelationGetRelationName(table_rel));
				
				if (SPI_execute(query, true, 0) == SPI_OK_SELECT && 
					SPI_processed > 0)
				{
					tuptable = SPI_tuptable;
					max_counter_datum = SPI_getbinval(tuptable->vals[0], 
						tuptable->tupdesc, 1, &isnull);
					if (!isnull)
					{
						max_counter = DatumGetInt64(max_counter_datum);
						elog(LOG, "Recovered counter for table %s (OID %u): %llu", 
							 RelationGetRelationName(table_rel), table_oid, 
							 (unsigned long long)max_counter);
					}
				}
				SPI_finish();
			}
		}
		
		table_close(table_rel, AccessShareLock);
	}
	PG_CATCH();
	{
		/* Skip tables we can't access */
		FlushErrorState();
		max_counter = 0;
	}
	PG_END_TRY();
	
	return max_counter;
}

/*
 * BlockchainRecoverCountersFromTables
 *	Recover counter values by scanning all blockchain tables (DEPRECATED - causes startup crashes)
 */
static void
BlockchainRecoverCountersFromTables(void)
{
	/* This function is deprecated due to SPI startup issues */
	elog(LOG, "BlockchainRecoverCountersFromTables: Deprecated - using lazy recovery instead");

	/* Function body removed - was causing startup crashes due to SPI usage */
	/* Counter recovery now happens lazily when tables are first accessed */
}

/*
 * BlockchainRestoreCounters
 *		Read counter values from disk at startup, with fallback to table scanning
 */
void
BlockchainRestoreCounters(void)
{
	File		file;
	char		path[MAXPGPATH];
	Oid			table_oid;
	uint64		counter_value;
	BlockchainCounterEntry *entry;
	bool		found;

	if (!BlockchainCounterShmem)
		return;

	snprintf(path, sizeof(path), "%s", BLOCKCHAIN_COUNTER_FILE);

	file = PathNameOpenFile(path, O_RDONLY);
	if (file < 0)
	{
		/* File doesn't exist yet - this is normal for first startup */
		if (errno == ENOENT)
		{
			elog(LOG, "No counter file found - will use lazy recovery on table access");
			return;
		}
			
		ereport(WARNING,
				(errcode_for_file_access(),
				 errmsg("could not open blockchain counter file \"%s\": %m",
						path)));
		/* On file access error, rely on lazy recovery */
		elog(LOG, "File access failed - will use lazy recovery on table access");
		return;
	}

	/* Read all counter entries */
	off_t offset;
	
	offset = 0;
	while (FileRead(file, &table_oid, sizeof(Oid), offset, WAIT_EVENT_DATA_FILE_READ) == sizeof(Oid))
	{
		offset += sizeof(Oid);
		if (FileRead(file, &counter_value, sizeof(uint64), offset, WAIT_EVENT_DATA_FILE_READ) != sizeof(uint64))
		{
			ereport(WARNING,
					(errmsg("incomplete entry in blockchain counter file")));
			break;
		}
		offset += sizeof(uint64);

		/* Restore counter entry */
		LWLockAcquire(&BlockchainCounterShmem->ctl_lock, LW_EXCLUSIVE);
		
		entry = (BlockchainCounterEntry *) hash_search(BlockchainCounterShmem->counter_hash,
														&table_oid,
														HASH_ENTER,
														&found);
		if (!found)
		{
			entry->table_oid = table_oid;
			LWLockInitialize(&entry->lock, LWTRANCHE_FIRST_USER_DEFINED + 1);
		}
		
		/* Use the maximum of stored and current value to handle concurrent updates */
		if (!found || counter_value > entry->counter_value)
		{
			entry->counter_value = counter_value;
			entry->last_persisted = counter_value;
		}
		
		LWLockRelease(&BlockchainCounterShmem->ctl_lock);
	}

	FileClose(file);
}

/*
 * BlockchainDropTableCounter
 *		Remove counter entry when a blockchain table is dropped
 */
void
BlockchainDropTableCounter(Oid table_oid)
{
	if (!BlockchainCounterShmem)
		return;

	LWLockAcquire(&BlockchainCounterShmem->ctl_lock, LW_EXCLUSIVE);
	
	hash_search(BlockchainCounterShmem->counter_hash,
				&table_oid,
				HASH_REMOVE,
				NULL);
				
	LWLockRelease(&BlockchainCounterShmem->ctl_lock);
	
	/* Persist the change */
	BlockchainPersistCounters();
}

/*
 * BlockchainCounterStartup
 *		Initialize counters at system startup
 */
void
BlockchainCounterStartup(void)
{
	/* Register shutdown callback to persist counters */
	before_shmem_exit(blockchain_counter_shmem_exit, (Datum) 0);
	
	/* Only restore from file-based persistence (safe during startup) */
	/* Skip SPI-based table scanning which causes startup crashes */
	BlockchainRestoreCounters();
	
	/* Register transaction callbacks to handle rollbacks 
	 * NOTE: The actual rollback logic is disabled to maintain counter gaps
	 */
	RegisterXactCallback(blockchain_xact_callback, NULL);
	RegisterSubXactCallback(blockchain_subxact_callback, NULL);
	
	elog(LOG, "Blockchain counter system initialized with lazy recovery");
}

/*
 * BlockchainCounterShutdown
 *		Persist counters at system shutdown
 */
void
BlockchainCounterShutdown(void)
{
	BlockchainPersistCounters();
}

/*
 * blockchain_xact_callback - Handle transaction events
 *
 * On commit: Mark all reservations as committed
 * On abort: Roll back counter values to remove uncommitted reservations
 */
static void
blockchain_xact_callback(XactEvent event, void *arg)
{
	BlockchainCounterReservation *reservation, *next;
	
	switch (event)
	{
		case XACT_EVENT_COMMIT:
		case XACT_EVENT_PARALLEL_COMMIT:
		case XACT_EVENT_PREPARE:
			/* Mark all current reservations as committed */
			for (reservation = current_reservations; reservation != NULL; reservation = reservation->next)
			{
				reservation->committed = true;
			}
			break;
			
		case XACT_EVENT_ABORT:
		case XACT_EVENT_PARALLEL_ABORT:
			/* DO NOT roll back counter values - maintain immutability
			 * 
			 * For blockchain systems, counter gaps from failed transactions are correct:
			 * - Preserves audit trail of all access attempts
			 * - Prevents dangerous race conditions with concurrent transactions
			 * - Maintains blockchain immutability principles
			 */
			elog(LOG, "Transaction aborted - counter gaps are intentional for blockchain immutability");
			
			for (reservation = current_reservations; reservation != NULL; reservation = next)
			{
				next = reservation->next;
				
				/* Simply free the reservation without rolling back */
				elog(DEBUG1, "Cleaned up reservation for table OID %u, counter %llu (NOT rolled back)", 
					 reservation->table_oid, (unsigned long long)reservation->reserved_counter);
				pfree(reservation);
			}
			
			/* Clear the reservation list */
			current_reservations = NULL;
			break;
			
		default:
			break;
	}
}

/*
 * blockchain_subxact_callback - Handle subtransaction events
 */
static void
blockchain_subxact_callback(SubXactEvent event, SubTransactionId mySubid,
						   SubTransactionId parentSubid, void *arg)
{
	/* For now, we don't handle subtransactions specially */
	/* Main transaction callback will handle the rollback */
}

/*
 * BlockchainCleanupHashCache
 *		Remove old entries from hash cache when it gets too full
 *		This prevents OOM errors with large cumulative insert volumes
 */
static void
BlockchainCleanupHashCache(void)
{
	HASH_SEQ_STATUS status;
	BlockchainHashEntry *entry;
	BlockchainHashKey *keys_to_remove;
	int removed = 0;
	int total = 0;
	int target_remove;
	int i;

	/* Count total entries */
	hash_seq_init(&status, BlockchainCounterShmem->hash_cache);
	while ((entry = (BlockchainHashEntry *) hash_seq_search(&status)) != NULL)
	{
		total++;
	}

	/* If cache is > 90% full, remove oldest 50% of entries */
	if (total > (MAX_CACHED_HASHES * 9 / 10))
	{
		target_remove = total / 2;

		elog(LOG, "Hash cache cleanup: %d entries (%.1f%% full), removing ~%d entries",
			 total, (total * 100.0) / MAX_CACHED_HASHES, target_remove);

		/* Allocate array to collect keys to remove */
		keys_to_remove = (BlockchainHashKey *) palloc(target_remove * sizeof(BlockchainHashKey));

		/* Collect keys to remove (can't remove during iteration) */
		hash_seq_init(&status, BlockchainCounterShmem->hash_cache);
		while ((entry = (BlockchainHashEntry *) hash_seq_search(&status)) != NULL && removed < target_remove)
		{
			keys_to_remove[removed] = entry->key;
			removed++;
		}
		hash_seq_term(&status);

		/* Now remove all collected entries */
		for (i = 0; i < removed; i++)
		{
			hash_search(BlockchainCounterShmem->hash_cache,
						&keys_to_remove[i],
						HASH_REMOVE,
						NULL);
		}

		pfree(keys_to_remove);

		elog(LOG, "Hash cache cleanup: removed %d entries, %d remaining",
			 removed, total - removed);
	}
}

/*
 * BlockchainStoreHash
 *		Store a hash in shared memory for an uncommitted block
 *		This allows the next transaction to read it before the block is committed
 */
void
BlockchainStoreHash(Oid table_oid, uint64 counter, const unsigned char *hash)
{
	BlockchainHashKey key;
	BlockchainHashEntry *entry;
	bool found;

	if (!BlockchainCounterShmem)
		return;

	key.table_oid = table_oid;
	key.counter = counter;

	LWLockAcquire(&BlockchainCounterShmem->hash_cache_lock, LW_EXCLUSIVE);

	/* Check if cache needs cleanup before inserting */
	BlockchainCleanupHashCache();

	entry = (BlockchainHashEntry *) hash_search(BlockchainCounterShmem->hash_cache,
												&key,
												HASH_ENTER,
												&found);

	if (entry)
	{
		memcpy(entry->hash_data, hash, 32);
		entry->valid = true;

		elog(LOG, "BlockchainStoreHash: Stored hash %02x%02x%02x%02x... for table_oid=%u, counter=%llu, PID=%d",
			 entry->hash_data[0], entry->hash_data[1], entry->hash_data[2], entry->hash_data[3],
			 table_oid, (unsigned long long)counter, MyProcPid);
	}

	LWLockRelease(&BlockchainCounterShmem->hash_cache_lock);
}

/*
 * BlockchainGetCachedHash
 *		Retrieve a hash from shared memory cache
 *		Returns true if found, false otherwise
 */
bool
BlockchainGetCachedHash(Oid table_oid, uint64 counter, unsigned char *hash_out)
{
	BlockchainHashKey key;
	BlockchainHashEntry *entry;
	bool found = false;

	if (!BlockchainCounterShmem)
		return false;

	key.table_oid = table_oid;
	key.counter = counter;

	LWLockAcquire(&BlockchainCounterShmem->hash_cache_lock, LW_SHARED);

	entry = (BlockchainHashEntry *) hash_search(BlockchainCounterShmem->hash_cache,
												&key,
												HASH_FIND,
												NULL);

	if (entry && entry->valid)
	{
		memcpy(hash_out, entry->hash_data, 32);
		found = true;

		elog(LOG, "BlockchainGetCachedHash: Found cached hash %02x%02x%02x%02x... for table_oid=%u, counter=%llu, PID=%d",
			 hash_out[0], hash_out[1], hash_out[2], hash_out[3],
			 table_oid, (unsigned long long)counter, MyProcPid);
	}
	else
	{
		elog(LOG, "BlockchainGetCachedHash: NOT FOUND for table_oid=%u, counter=%llu, PID=%d",
			 table_oid, (unsigned long long)counter, MyProcPid);
	}

	LWLockRelease(&BlockchainCounterShmem->hash_cache_lock);

	return found;
}