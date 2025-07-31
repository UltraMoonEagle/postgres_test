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

/* Counter entry for each blockchain table */
typedef struct BlockchainCounterEntry
{
	Oid			table_oid;			/* OID of the blockchain table */
	uint64		counter_value;		/* Current counter value */
	uint64		last_persisted;		/* Last value persisted to disk */
	LWLock		lock;				/* Per-table counter lock */
} BlockchainCounterEntry;

/* Shared memory structure for blockchain counters */
typedef struct BlockchainCounterData
{
	LWLock		ctl_lock;			/* Control lock for the hash table */
	HTAB	   *counter_hash;		/* Hash table of counter entries */
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

#endif /* BLOCKCHAIN_COUNTER_H */