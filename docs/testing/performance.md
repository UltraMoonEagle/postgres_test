# Performance Testing

See [Concurrency Tests](concurrency-tests.md) for performance benchmarks.

## Key Metrics

- INSERT: 30,000-50,000 rows/sec
- Hash computation: 2-5 μs per row
- Counter increment: <1 μs
- Cache lookup: <1 μs (hit), ~50 μs (miss)
