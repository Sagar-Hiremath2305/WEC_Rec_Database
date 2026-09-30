# Build Your Own Redis (C++17)

Welcome to this custom, from-scratch implementation of a Redis-compatible in-memory data store. This project strictly adheres to all foundational constraints: it features a custom open-addressing hashtable, a single-threaded event loop, manual memory tracking, and AOF crash recovery—all built entirely without async frameworks, garbage collection, or standard library hash maps.

## Features
* **Custom Hashtable:** Open-addressing dictionary with strict `PROBE_CAP` limits and zero-pause incremental resizing across two live tables.
* **Single-Threaded Event Loop:** High-performance multiplexing using `kqueue` (macOS), maintaining strict per-connection state machines without blocking.
* **Write-Ahead Log (AOF):** Configurable crash recovery with three fsync modes (`always`, `everysec`, `no`), sequence numbers, checksum validation, and background log compaction (rewrite).
* **Memory & Expiry Management:** Exact byte tracking (via overloaded `operator new/delete`) avoiding `/proc` polling. Expiry combines lazy evaluation on-read with a wall-clock bounded active background sampler. Eviction strictly follows approximate LRU across all 5 standard policies.
* **Data Types Supported:** Strings, Lists, Hashes, Sets, and Sorted Sets (using a custom Skiplist).

## Build and Run
```bash
# Build the server (Requires a C++17 compatible compiler like Clang or GCC)
make

# Run the server on port 6379
./redis_server
```

---

## Theoretical Justifications (Grading Requirements)

### Phase 1: Protocol Parsing & Error Handling
The RESP parser implements a resumable, byte-by-byte state machine capable of handling partial reads seamlessly.
- **Recoverable errors:** Commands with incorrect arity, wrong type operations, and unrecognized commands return a standard `-ERR` or `-WRONGTYPE` reply. The parser state safely resets for the next command in the buffer, leaving the connection entirely usable.
- **Fatal errors:** Malformed RESP syntax (e.g., missing `\r\n` where explicitly required by the protocol layout), oversized bulk strings (`> 512MB`), and EOF/socket errors immediately force a connection close to protect the server state.

### Phase 3: Hashtable & SCAN Cursor
To support `SCAN`, Redis uses a **Reverse Binary Iteration** algorithm for its cursor. Instead of incrementing the cursor normally (`c++`), it increments the reversed bits of the cursor.
*Why it survives a resize:* When a hashtable doubles in size, a single bucket `X` splits into `X` and `X + old_size`. Because reverse binary iteration iterates the highest-order bits first, it ensures that if a table expands, the newly split buckets are visited sequentially in the new bits space without revisiting already scanned elements. This guarantees no elements are missed or duplicated due to rehashing mid-iteration.

### Phase 4: Exact LRU vs Approximate LRU
Exact LRU using an intrusive doubly-linked list is unacceptable for two main reasons:
1. **Memory Overhead:** An intrusive linked list requires at least two 8-byte pointers (`prev` and `next`) per key. For millions of small keys, this is a massive overhead (e.g., 16MB just in pointers for 1 million keys), destroying overall memory efficiency.
2. **Pointer-chasing Cost (Cache Misses):** Every time a key is accessed, moving it to the head of the LRU list requires dereferencing and updating multiple pointers scattered randomly across memory. This causes cache misses and stalls the CPU pipeline, ruining the throughput of a single-threaded event loop. Our approximate LRU (timestamp sampling) entirely avoids both issues.

### Phase 5: Durable Rename
When rewriting the AOF log, simply creating a `.tmp` file, calling `fsync()` on the file, and then calling `rename()` is not sufficient to guarantee durability across a sudden power loss.
*The Missing Step:* The `rename()` operation modifies the directory's metadata in the OS page cache. If power is lost immediately after, the file contents safely exist on disk, but the directory entry mapping `appendonly.aof` to that inode is lost.
*Solution:* We must `open()` the parent directory (`"."`) itself and call `fsync()` on the directory file descriptor immediately after the `rename()`. This flushes the directory metadata to disk, making the rename completely durable.

### Bonus: Why a Skiplist instead of a Balanced Tree?
Sorted Sets (`ZSET`) are implemented using a custom Skiplist paired with our Hashtable rather than a Red-Black or AVL tree.
1. **Concurrency and Simplicity:** Skiplists are fundamentally simpler to implement and reason about. There are no complex tree-balancing rotations.
2. **Range Operations:** Skiplists easily support O(1) sequential traversal at the bottom level (`level[0].forward`), making operations like `ZRANGE` exceptionally fast compared to in-order tree traversal.
3. **Rank Calculations:** By storing the `span` (number of nodes skipped) in each level's forward pointer, calculating `ZRANK` becomes an O(log N) operation rather than O(N).

### Bonus: Serverless Deployment & Scale-to-Zero
A traditional stateful, single-process, in-memory database like Redis is fundamentally opposed to scale-to-zero serverless runtimes (like Google Cloud Run or AWS App Runner):
1. **Cold Starts:** Startup time is dominated by replaying the AOF log. If a container scales to zero, the next incoming request must wait for the entire AOF log (potentially gigabytes) to be read before receiving a reply, completely destroying in-memory cache latency.
2. **Wall-clock Expiry Gaps:** When the instance is suspended, background expiry stops. Time continues, meaning when it spins back up, thousands of keys may have expired instantly, causing massive eviction spikes.
3. **File Lock Contention:** Serverless platforms scale out by spinning up multiple instances. If two instances attach to the same persistent volume and attempt to append to the same AOF file simultaneously, data corruption is guaranteed. 
*Serverless-native designs (like Momento or Upstash) resolve this by decoupling the compute layer from the storage layer, routing stateless proxies to highly available backend storage shards.*

---

## Phase 5: Crash Bug Log & Resolution

Building the AOF crash recovery was the most failure-prone phase of the project. Below is a log of the primary crash bug encountered, how it was reproduced, and the steps taken to resolve it.

### **Bug 1: Torn RESP Reads during `SIGKILL`**
**The Failure:** 
Initially, the AOF manager simply appended raw RESP strings (e.g., `*3\r\n$3\r\nSET...`) to `appendonly.aof`. During crash testing, the grading script would send a `SIGKILL` at a uniformly random point—frequently in the exact middle of an OS `write()` syscall.
Upon restart, the server would read the torn RESP string (e.g., it would see `$5\r\nalic` and hit EOF). Because the parser was expecting the closing `\r\n`, it would hang or throw a protocol error, preventing the server from starting up. Worse, if the torn write happened to look like valid RESP by pure chance, it resurrected corrupted data.

**The Reproduction Harness:**
To reproduce this deterministically instead of hoping for a random `SIGKILL` collision, I built a bash script that:
1. Spun up the server.
2. Piped 100,000 `SET` commands into it via `nc`.
3. Used `dd` to intentionally truncate `appendonly.aof` at random byte boundaries (e.g., `truncate -s 14502 appendonly.aof`).
4. Restarted the server to observe the exact crash.

**The Resolution:**
Attempting to parse backwards or detect torn writes purely via RESP text syntax was too brittle. I scrapped the raw-RESP approach and redesigned the log formatting. 
I implemented a **16-byte fixed binary header** for every AOF record: `[8-byte sequence number][4-byte length][4-byte checksum]`. 
Now, on startup:
1. The server reads exactly 16 bytes. If `read()` returns `< 16`, it instantly knows it's a trailing half-written record and truncates the file cleanly.
2. It reads `length` bytes. If it hits EOF prematurely, it truncates.
3. It computes the CRC32-style checksum of the payload. If it doesn't match the header, it identifies data corruption, discards the record, and stops replay securely. 
This definitively solved all torn-write crashes.

### **Bug 2: Double-Free Crash during `SET` command (Rule of Three Violation)**
**The Failure:** 
During testing, sending a simple `SET mykey "Hello"` command caused the server to immediately crash and terminate. The process exited with a fatal error from the memory allocator indicating a "double free detected".

**The Reproduction Harness:**
To reproduce, I simply compiled the server (`make clean && make`), started it, and used a one-liner Python script to send the `SET` command via TCP:
```bash
python3 -c "import socket; s=socket.socket(); s.connect(('127.0.0.1', 6379)); s.sendall(b'*3\r\n\$3\r\nSET\r\n\$5\r\nmykey\r\n\$5\r\nHello\r\n')"
```
The server crashed instantly upon receiving the payload.

**The Resolution:**
Tracing the code revealed a severe Rule of Three/Five violation in the `DictValue` struct. `DictValue` managed a dynamically allocated raw pointer (`void* ptr`) and had a custom destructor to `delete` it. However, it relied on the implicitly generated copy constructor. 
When the command was parsed, `execute_command` constructed a temporary `DictValue` and passed it by value via `std::move` to `Dict::set`. Because move semantics were not defined, it invoked a shallow copy. The same shallow copy occurred inside `Dict::set` when assigning to the hash table entry. When the temporary variables went out of scope, their destructors double-deleted the exact same memory address. 
To fix this, I completely disabled copy semantics for `DictValue` (by deleting the copy constructor and copy assignment operator) and implemented a strict, zero-allocation `noexcept` move constructor and move assignment operator using pointer swapping. This safely transferred memory ownership without triggering duplicate frees.
