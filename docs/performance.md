# Performance and a reproducible benchmark

Execution moves the pending batch into the job and restores it if enqueueing is rejected;
it does not duplicate the entire batch on each execute call. Each session retains one
unnamed prepared statement. Reusing SQL and OIDs avoids repeated Parse operations without
accumulating plans in PostgreSQL. The default FIFO lane preserves order. Use other lanes
only when the application's concurrency rules permit it.

Twenty snapshots still mean twenty SQL statements, each with its own response and potential
failure. One UNNEST statement can modify twenty rows in a single execution. The gamemode
defines array lengths, duplicate handling, and SQL rules; UPDATE is never transformed
automatically into bulk SQL.

`tests/benchmark.cpp` compares three modes on a temporary table and checks that every row
received exactly three increments:

```sh
cmake -S . -B build -DPG_BUILD_BENCHMARK=ON
cmake --build build --parallel
python tests/run_postgres.py --build build --benchmark
```

One local run with 1000 rows, Windows x64, GCC 16.1, libpq 18.4, PostgreSQL 17.11, and
TLS verify-full over loopback produced:

| Mode                             | SQL statements | Total microseconds |
| -------------------------------- | -------------: | -----------------: |
| Individual prepared queries      |           1000 |             154686 |
| Pipeline with Sync per statement |           1000 |              73146 |
| One UNNEST statement             |              1 |               8010 |

This is a verification sample, not a statistical distribution or performance guarantee.
It measures libpq directly, excluding Pawn, ticks, queues, and game-server contention.
Pipeline uses windows of 32 operations and Sync per operation to preserve implicit transaction
boundaries. Omitting Sync may change atomicity and error propagation.

For production comparisons, record hardware, exact versions, TLS, RTT, row counts, indexes,
and contention. Repeat after warmup and report percentiles and peak memory. Measure callback
latency separately. Resource limits bound retained objects, but libpq can allocate a large
row or message before the component applies its result limit.
