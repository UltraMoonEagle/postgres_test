/*-------------------------------------------------------------------------
 *
 * blockchain_counter.h
 *	  Global counter infrastructure for blockchain tables
 *
 * This module provides a global counter system that replaces the LSN-based
 * chaining mechanism, eliminating the need for double inserts.
 *
 * Portions Copyright (c) 1996-2025, PostgreSQL Global Development Group
 *
 * src/include/blockchain/blockchain_counter.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef BLOCKCHAIN_COUNTER_H
#define BLOCKCHAIN_COUNTER_H

#include "postgres.h"
#include "storage/lwlock.h"
#include "storage/shmem.h"
#include "utils/hsearch.h"

/* Maximum number of blockchain tables that can have counters */
#define MAX_BLOCKCHAIN_TABLES 1024

/* Maximum number of uncommitted hashes to cache */
#define MAX_CACHED_HASHES 10000

/* Counter entry for each blockchain table */
typedef struct BlockchainCounterEntry
{
	Oid			table_oid;			/* OID of the blockchain table */
	uint64		counter_value;		/* Current counter value */
	uint64		last_persisted;		/* Last value persisted to disk */
	LWLock		lock;				/* Per-table counter lock */
} BlockchainCounterEntry;

/* Hash cache key for uncommitted blocks */
typedef struct BlockchainHashKey
{
	Oid			table_oid;			/* OID of the blockchain table */
	uint64		counter;			/* Counter value for this hash */
} BlockchainHashKey;

/* Hash cache entry for uncommitted blocks */
typedef struct BlockchainHashEntry
{
	BlockchainHashKey key;			/* Hash key */
	unsigned char hash_data[32];	/* The actual SHA256 hash (fixed 32 bytes) */
	bool		valid;				/* Whether this hash is valid */
} BlockchainHashEntry;

/* Shared memory structure for blockchain counters */
typedef struct BlockchainCounterData
{
	LWLock		ctl_lock;			/* Control lock for the hash table */
	HTAB	   *counter_hash;		/* Hash table of counter entries */
	LWLock		hash_cache_lock;	/* Lock for the hash cache */
	HTAB	   *hash_cache;			/* Cache of uncommitted hashes */
} BlockchainCounterData;

/* Global pointer to shared memory data */
extern BlockchainCounterData *BlockchainCounterShmem;

/* Function declarations */
extern void BlockchainCounterShmemInit(void);
extern Size BlockchainCounterShmemSize(void);
extern uint64 BlockchainGetNextCounter(Oid table_oid);
extern void BlockchainPersistCounters(void);
extern void BlockchainRestoreCounters(void);
extern void BlockchainDropTableCounter(Oid table_oid);

/* Counter persistence functions */
extern void BlockchainCounterStartup(void);
extern void BlockchainCounterShutdown(void);

/* Hash cache functions for uncommitted blocks */
extern void BlockchainStoreHash(Oid table_oid, uint64 counter, const unsigned char *hash);
extern bool BlockchainGetCachedHash(Oid table_oid, uint64 counter, unsigned char *hash_out);

#endif /* BLOCKCHAIN_COUNTER_H */