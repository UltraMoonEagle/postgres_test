# Known Limitations

## Current Limitations

1. **No UPDATE/DELETE**: Tables are append-only
2. **No schema modifications**: Cannot ALTER TABLE after creation
3. **No partitioning**: Native partitioning not yet supported
4. **Cache eviction**: Hash cache grows to 10,000 entries (no LRU)
5. **Counter gaps**: Aborted transactions leave gaps (by design)

## Workarounds

- **Updates**: Insert new version, query for `MAX(__tx_lsn)` per key
- **Schema changes**: Create new table, copy data
- **Large tables**: Use time-based archiving

See [Changelog](changelog.md) for upcoming features.
