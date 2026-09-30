# Build Your Own Redis (C++17)

Welcome to this custom, from-scratch implementation of a Redis-compatible in-memory data store. This project features a custom open-addressing hashtable, a single-threaded event loop, manual memory tracking, and AOF crash recovery.

## Features
* **Custom Hashtable:** Open-addressing dictionary with strict limits and incremental resizing.
* **Event Loop:** High-performance multiplexing using `kqueue` (macOS).
* **Write-Ahead Log (AOF):** Configurable crash recovery (`always`, `everysec`, `no`) with background rewrite.
* **Memory Management:** Exact byte tracking and approximate LRU eviction.
* **Data Types:** Strings, Lists, Hashes, Sets, and Sorted Sets (using a Skiplist).

## Build and Run
```bash
# Build the server (Requires a C++17 compatible compiler like Clang or GCC)
make

# Run the server on port 6379
./redis_server
```

## Supported Commands
You can interact with the server using standard `redis-cli`, `netcat`, or custom clients over TCP.

**Strings & Keys:**
* `SET key value [EX seconds] [PX milliseconds] [NX|XX]`
* `GET key`
* `DEL key1 key2 ...`
* `PING [message]`
* `ECHO message`

**Hashes:**
* `HSET key field value [field value ...]`
* `HGET key field`
* `HDEL key field [field ...]`
* `HGETALL key`

**Sets & Sorted Sets:**
* `SADD key member [member ...]`
* `SMEMBERS key`
* `ZADD key score member`
* `ZRANGE key start stop`
* `ZSCORE key member`

**Transactions:**
* `MULTI`, `EXEC`, `DISCARD`, `WATCH`

---

## Theoretical Justifications

### Phase 1: Protocol Parsing
- **Recoverable errors:** Commands with incorrect arity or type return `-ERR`. The parser safely resets.
- **Fatal errors:** Malformed syntax or oversized payloads force a connection close to protect memory.

### Phase 3: Hashtable & SCAN Cursor
- **Reverse Binary Iteration:** Iterating reversed bits ensures that if a table expands, newly split buckets are visited seamlessly without duplicates or missed elements mid-iteration.

### Phase 4: Approximate LRU
- **Why not Exact LRU?** Exact LRU needs a linked list, adding massive memory overhead (pointers) and causing CPU cache misses from pointer-chasing. Timestamp sampling achieves eviction with better throughput.

### Phase 5: Durable Rename
- **The missing fsync:** `rename()` updates the OS cache. To survive sudden power loss, we must immediately `open()` and `fsync()` the parent directory to flush directory metadata to disk.

### Bonus: Skiplist vs Balanced Tree
- **Simpler concurrency:** No complex rebalancing rotations.
- **Faster ranges:** O(1) sequential traversal at the bottom level (`ZRANGE`).
- **Fast ranking:** Storing skip spans enables O(log N) `ZRANK` operations.

### Bonus: Serverless Deployment
- **Why it fails:** Scale-to-zero serverless platforms suffer from slow AOF replay cold-starts, sudden mass evictions from paused clocks, and storage contention. Caches must decouple compute from storage to scale effectively.

---

## Phase 5: Crash Bug Log & Resolution

Building the AOF crash recovery was the most failure-prone phase. Below are the primary crash bugs encountered and resolved.

### Bug 1: Torn RESP Reads during SIGKILL
**The Failure:** 
The grading script sent `SIGKILL` mid-write, tearing the RESP string on disk. On restart, the parser hung or threw errors, halting startup.

**The Reproduction Harness:**
Piped 100,000 `SET` commands via `nc`, randomly truncated `appendonly.aof` using `dd`, and restarted to observe the crash.

**The Resolution:**
I added a 16-byte fixed binary header `[8-byte seq][4-byte len][4-byte checksum]` to every AOF record. Startup now validates length and checksums, instantly detecting and discarding torn writes.

### Bug 2: Double-Free Crash during SET command
**The Failure:** 
Sending `SET mykey "Hello"` crashed the server immediately with a "double free detected" error.

**The Reproduction Harness:**
Used a one-liner Python script to send the `SET` command via TCP:
```bash
python3 -c "import socket; s=socket.socket(); s.connect(('127.0.0.1', 6379)); s.sendall(b'*3\r\n$3\r\nSET\r\n$5\r\nmykey\r\n$5\r\nHello\r\n')"
```

**The Resolution:**
Identified a Rule of Three violation in the `DictValue` struct. It manually deleted a raw pointer but relied on implicit shallow copies. Passing the struct by value caused two destructors to double-delete the same pointer. I resolved this by deleting copy semantics and implementing strict move semantics (`noexcept` pointer swapping).
