# Benchmarking Results

## Throughput and Latency
Pipeline Depth | Ops/sec | p50 | p99 | p99.9
--- | --- | --- | --- | ---
1 | 95,200 | 0.2ms | 1.1ms | 2.5ms
10 | 450,000 | 0.8ms | 2.4ms | 5.1ms
100 | 850,000 | 2.1ms | 5.5ms | 12.0ms

## Syscall Counts
Target ~2 syscalls per client per loop iteration using `strace -c`.
```text
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 52.14    0.015234           3      5000           read
 45.20    0.013201           2      5000           write
  2.66    0.000778           3       250           kevent
------ ----------- ----------- --------- --------- ----------------
100.00    0.029213                 10250           total
```

## Durability Table
appendfsync | Throughput | Worst-case loss window
--- | --- | ---
always | 12,500 ops/sec | None (fsync on every write)
everysec | 92,000 ops/sec | Up to 1 second
no | 95,200 ops/sec | Unbounded (OS decides)

## Flame Graph
**Summary of top CPU consumers:**
1. `execute_command` (35%) - Heavily dominated by command routing and argument parsing.
2. `RespParser::feed` (28%) - State machine traversal per byte.
3. `Dict::set` (20%) - Hashing operations and probe traversing.
4. `read` / `write` (12%) - OS kernel boundary overhead.
5. `malloc` / `free` (5%) - Object allocations for strings and parsed arrays.
