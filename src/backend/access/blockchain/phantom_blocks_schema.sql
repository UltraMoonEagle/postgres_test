-- Phantom Blocks Table: Stores metadata for allocated-but-uncommitted blockchain counters
-- This ensures cryptographic verification remains possible even after rollbacks and restarts

CREATE TABLE IF NOT EXISTS blockchain_phantom_blocks (
    table_oid OID NOT NULL,           -- Which blockchain table
    counter BIGINT NOT NULL,           -- The allocated counter value
    prev_hash BYTEA NOT NULL,          -- Hash of previous block (for chain reconstruction)
    computed_hash BYTEA NOT NULL,      -- Hash computed for this phantom block
    allocated_at TIMESTAMP NOT NULL,   -- When counter was allocated
    xid XID NOT NULL,                  -- Transaction ID that allocated it
    status TEXT NOT NULL,              -- 'ALLOCATED', 'COMMITTED', 'ABORTED'

    PRIMARY KEY (table_oid, counter)
);

-- Index for cleanup queries
CREATE INDEX IF NOT EXISTS idx_phantom_blocks_status
    ON blockchain_phantom_blocks(status);

COMMENT ON TABLE blockchain_phantom_blocks IS
    'Stores metadata for blockchain counters allocated by uncommitted or rolled-back transactions. '
    'Required for cryptographic chain verification after cache flush or restart.';
