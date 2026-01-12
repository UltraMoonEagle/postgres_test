/*-------------------------------------------------------------------------
 *
 * blockchain_phantom_optimized.h
 *	  Zero-overhead phantom blocks - only persist on rollback
 *
 * IDENTIFICATION
 *	  src/include/blockchain/blockchain_phantom_optimized.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef BLOCKCHAIN_PHANTOM_OPTIMIZED_H
#define BLOCKCHAIN_PHANTOM_OPTIMIZED_H

#include "postgres.h"

/*
 * Record phantom block (writes to append-only log file - sequential I/O)
 */
extern void blockchain_record_phantom_block(Oid table_oid, uint64 counter,
                                           const unsigned char *prev_hash,
                                           const unsigned char *computed_hash);

/*
 * Load phantom blocks from log file into database table (crash recovery)
 * Call this during server startup
 */
extern void blockchain_recover_phantom_blocks(void);

#endif   /* BLOCKCHAIN_PHANTOM_OPTIMIZED_H */
