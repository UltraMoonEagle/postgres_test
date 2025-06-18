    /*-------------------------------------------------------------------------
 *
 * blockchainam.h
 *	  Header for the blockchain table access method.
 *
 * Portions Copyright (c) 1996-2025, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/access/blockchainam.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef BLOCKCHAINAM_H
#define BLOCKCHAINAM_H

#include "access/tableam.h"
#include "nodes/parsenodes.h"
#include "catalog/pg_type.h"

typedef struct BlockchainColumnDef
{
    const char *name;		/* Column name */
    Oid			type_oid;	/* OID of the column type */
    int			typmod;		/* Type modifier, -1 if not applicable */
	int 		attno;		/* Attribute number, -1 if not applicable */
} BlockchainColumnDef;


extern const TableAmRoutine *GetBlockchainTableAmRoutine(void);
extern const TableAmRoutine blockchainam_methods;

static BlockchainColumnDef blockchain_system_columns[] = {
	{"__row_id", UUIDOID, -1, -1},
	{"__curr_hash", BYTEAOID, -1, -1},
	{"__prev_hash", BYTEAOID, -1, -1},
	{"__tx_type", TEXTOID, -1, -1},
	{"__tx_lsn", PG_LSNOID, -1, -1},
	{"__tx_origin", UUIDOID, -1, -1},
	{"__tx_version", INT4OID, -1, -1},
	{"__is_latest", BOOLOID, -1, -1},
	{"__tx_timestamp", TIMESTAMPTZOID, -1,-1}
};

#define NUM_BLOCKCHAIN_COLUMNS 8

#endif							/* BLOCKCHAINAM_H */