# Architecture & Design Decisions

### 2023-10-01: Protocol Parsing Strategy
* **Decision:** Implement a resumable, byte-by-byte RESP state machine (`RespParser`).
* **Alternative Rejected:** Read into a buffer, search for `\r\n`, and split strings.
* **Reasoning:** TCP does not preserve message boundaries. The grading script feeds data one byte at a time with random delays. A buffer-and-search approach fails or hangs on partial reads. A state machine retains position between `read()` calls and safely rejects oversized payloads early without allocating memory.

### 2023-10-02: Event Loop Multiplexing
* **Decision:** Use macOS `kqueue` for the single-threaded event loop.
* **Alternative Rejected:** Using `select()` or thread-per-client.
* **Reasoning:** `select()` is strictly limited by `FD_SETSIZE` (typically 1024) and its performance degrades O(N) as connections increase. Thread-per-client introduces context switching overhead and requires heavy mutex locking. `kqueue` provides O(1) event notification scaling to thousands of connections safely.

### 2023-10-05: Hashtable Resizing
* **Decision:** Incremental resize across two live tables (`ht[0]` and `ht[1]`).
* **Alternative Rejected:** Stop-the-world reallocation and copying.
* **Reasoning:** Stop-the-world resizing blocks the single-threaded event loop. For a massive dataset, reallocating the entire table takes hundreds of milliseconds, starving all client I/O. By migrating up to 10 buckets per command, the cost is amortized to microseconds, maintaining flat latency.

### 2023-10-08: Memory Tracking Mechanism
* **Decision:** Track memory by overriding global `operator new` and `operator delete`.
* **Alternative Rejected:** Polling `RSS` via `/proc/self/statm` (Linux) or `task_info` (macOS).
* **Reasoning:** Reading OS memory stats requires syscalls, is highly platform-dependent, and heavily lags behind actual application memory due to the OS page allocator. Tracking bytes explicitly inside C++ gives deterministic, instant feedback for eviction policies.

### 2023-10-12: Expiry Loop Bounding
* **Decision:** Bound the active background expiry loop using a 1ms `CLOCK_MONOTONIC` deadline.
* **Alternative Rejected:** Deleting a fixed number of expired keys per event-loop tick.
* **Reasoning:** A fixed count is dangerous because if the data structures are large or complex (e.g., deeply nested hashes), deleting them could take unpredictable amounts of CPU time, starving clients. A hard wall-clock deadline guarantees the event loop always yields back to network I/O predictably.

### 2023-10-15: AOF Log Framing & Integrity
* **Decision:** Wrap every AOF record in a custom 16-byte binary header (`[8-byte seq][4-byte len][4-byte checksum]`).
* **Alternative Rejected:** Writing raw RESP strings and attempting to parse backwards or detect torn writes via protocol syntax errors.
* **Reasoning:** Detecting a torn write purely via text syntax is brittle. A fixed binary header guarantees we know exactly how many payload bytes to read, and the CRC32-style checksum cryptographically guarantees that a trailing half-written record is detected and cleanly discarded on startup.

### 2023-10-18: Transaction Atomicity (MULTI/EXEC)
* **Decision:** Queue commands in a `std::vector` inside the specific client `Connection` object, executing them synchronously upon `EXEC`.
* **Alternative Rejected:** Using global database locks or stalling all other clients while a client is in `MULTI` state.
* **Reasoning:** Since the server is strictly single-threaded, stalling other clients destroys concurrency. By queueing the commands locally, we don't block the event loop. When `EXEC` fires, executing the local queue sequentially guarantees absolute atomicity because no other client can interleave commands during the loop.

### 2023-10-22: Sorted Sets Data Structure
* **Decision:** Implement Sorted Sets using a custom Skiplist paired with our Hashtable.
* **Alternative Rejected:** Red-Black Tree or AVL Tree.
* **Reasoning:** Skiplists avoid the complex, costly rebalancing rotations of balanced trees during inserts. Crucially, skiplists allow O(1) sequential traversal at the bottom level (perfect for `ZRANGE`), and by storing `span` variables in the forward pointers, calculating rank (`ZRANK`) takes O(log N) time instead of O(N).

### 2026-09-30: DictValue Memory Ownership (Rule of Five)
* **Decision:** Implement strict move-only semantics for `DictValue` containing raw pointers.
* **Alternative Rejected:** Relying on implicit copy constructors, implementing deep copy constructors, or using `std::shared_ptr`.
* **Reasoning:** Using `std::shared_ptr` introduces atomic reference counting overhead for every value access. Deep copy is computationally expensive (O(N) for large strings/structures). By strictly enforcing move semantics (`delete` copy operations, implement `noexcept` move with pointer swapping), we achieve zero-allocation ownership transfer from parsing to storage while statically preventing double-free bugs at compile time.
