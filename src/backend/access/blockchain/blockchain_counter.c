/*-------------------------------------------------------------------------
 *
 * blockchain_counter.c
 *	  Global counter infrastructure for blockchain tables
 *
 * This module implements a shared memory-based counter system that provides
 * monotonically increasing counter values for blockchain tables. This
 * replaces the LSN-based approach and eliminates double inserts.
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

/* Global pointer to shared memory data */
BlockchainCounterData *BlockchainCounterShmem = NULL;

/* File to persist counter values */
#define BLOCKCHAIN_COUNTER_FILE "global/blockchain_counters"

/*
 * BlockchainCounterShmemSize
 *		Compute space needed for blockchain counter shared memory
 */
Size
BlockchainCounterShmemSize(void)
{
	Size		size;

	size = MAXALIGN(sizeof(BlockchainCounterData));
	size = add_size(size, hash_estimate_size(MAX_BLOCKCHAIN_TABLES,
											  sizeof(BlockchainCounterEntry)));

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
	}
}

/*
 * BlockchainGetNextCounter
 *		Get the next counter value for a blockchain table
 *
 * This function is the main entry point for obtaining counter values.
 * It uses atomic operations to ensure thread safety.
 */
uint64
BlockchainGetNextCounter(Oid table_oid)
{
	BlockchainCounterEntry *entry;
	bool		found;
	uint64		result;

	if (!BlockchainCounterShmem)
		elog(ERROR, "blockchain counter shared memory not initialized");

	/* Look up or create counter entry */
	LWLockAcquire(&BlockchainCounterShmem->ctl_lock, LW_EXCLUSIVE);

	entry = (BlockchainCounterEntry *) hash_search(BlockchainCounterShmem->counter_hash,
													&table_oid,
													HASH_ENTER,
													&found);

	if (!found)
	{
		/* Initialize new counter entry */
		entry->table_oid = table_oid;
		entry->counter_value = 1;  /* Start from 1 */
		entry->last_persisted = 0;
		LWLockInitialize(&entry->lock, LWTRANCHE_FIRST_USER_DEFINED + 1);
	}

	LWLockRelease(&BlockchainCounterShmem->ctl_lock);

	/* Atomically increment and get counter value */
	LWLockAcquire(&entry->lock, LW_EXCLUSIVE);
	result = entry->counter_value++;
	
	/* Persist every 1000 increments to reduce I/O */
	if (entry->counter_value - entry->last_persisted >= 1000)
	{
		BlockchainPersistCounters();
		entry->last_persisted = entry->counter_value;
	}
	
	LWLockRelease(&entry->lock);

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
	
	off_t offset = 0;
	hash_seq_init(&status, BlockchainCounterShmem->counter_hash);
	while ((entry = (BlockchainCounterEntry *) hash_seq_search(&status)) != NULL)
	{
		/* Write table OID and counter value */
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
		
		if (FileWrite(file, &entry->counter_value, sizeof(uint64), offset, WAIT_EVENT_DATA_FILE_WRITE) != sizeof(uint64))
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
 * BlockchainRestoreCounters
 *		Read counter values from disk at startup
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
		/* File doesn't exist yet, which is OK */
		if (errno == ENOENT)
			return;
			
		ereport(WARNING,
				(errcode_for_file_access(),
				 errmsg("could not open blockchain counter file \"%s\": %m",
						path)));
		return;
	}

	/* Read all counter entries */
	off_t offset = 0;
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
	BlockchainRestoreCounters();
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