# Shared Memory Cache Architecture

The shared memory cache is documented in [Data Flow](data-flow.md#hash-cache-flow) and [Counter Management](counter-management.md).

## Overview

- 10,000-entry hash cache for uncommitted transaction hashes
- LWLock-protected concurrent access
- Enables cross-transaction hash visibility
- Resolves hash chain branching

See [Counter Management](counter-management.md) for implementation details.
