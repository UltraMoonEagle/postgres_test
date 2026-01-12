/*-------------------------------------------------------------------------
 *
 * blockchain_phantom.h
 *	  Phantom blocks persistence for blockchain tables
 *
 * IDENTIFICATION
 *	  src/include/blockchain/blockchain_phantom.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef BLOCKCHAIN_PHANTOM_H
#define BLOCKCHAIN_PHANTOM_H

#include "postgres.h"

/*
 * Persist phantom block metadata (called immediately after counter allocation)
 */
extern void blockchain_persist_phantom_block(Oid table_oid, uint64 counter,
                                             const unsigned char *prev_hash,
                                             const unsigned char *computed_hash);

/*
 * Mark phantom block as committed (transaction commit hook)
 */
extern void blockchain_mark_phantom_committed(Oid table_oid, uint64 counter);

/*
 * Mark phantom block as aborted (transaction abort hook)
 */
extern void blockchain_mark_phantom_aborted(Oid table_oid, uint64 counter);

/*
 * Cleanup old committed phantom blocks
 */
extern void blockchain_cleanup_phantom_blocks(Oid table_oid);

#endif   /* BLOCKCHAIN_PHANTOM_H */
