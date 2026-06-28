# OffsetDB

## Description

A lightweight, multi-threaded key-value database server build in C++17.
It uses an append-only log and in-memory offset indexing for fast disk loopups.

Instead of scanning the file or loading the entire database into RAM, OffsetDB jumps instantly to the exact byte on the disk to retrieve data.

Interactions are handled through a CLI built in Python.

## Key Features

* **Custom Thread Pool** written from scratch to handle TCP connections.
* **Reader-Writer Locks** (shared_mutex) allows fully parallel reads from multiple clients, only locking the database when performing a write operation.
* **Low RAM footprint:** Instead of loading values into RAM, the database keeps the keys and their offsets in memory.
* **Thread-Local File I/O** for optimal disk reads, each thread keeping its own read stream.
* **Crash Recovery by Design:** The append-only log naturally acts as a Write-Ahead Log. On startup, the in-memory index is rebuilt by sequentially reading the log.
* **Log Compaction & Versioning:** The COMPACT command cleans up stale data by creating a new file without blocking active readers.
* **TCP Stream Buffer Accumulator:** The buffer accumulates fragmented packets and only parses/executes commands when it detects a newline (\n).
* **Zero Dependencies:** The project is built entirely using C++17 and native Winsock APIs.


## Prerequisites

* **Operating System:** Windows (relies on `winsock2.h` and `Ws2_32.lib`)
* **Compiler:** A C++17 compliant compiler (e.g., MSVC)
* **IDE:** Visual Studio (Recommended, uses standard `.sln` / `.vcxproj`)
* **Python 3.x**

## Run the Server

* Clone the repository
* Open the .sln file in Visual Studio.
* Build the solution (Release or Debug).
* Execute the compiled binary.
* The server will automatically load existing data from database_log.txt (or create it if it doesn't exist) and start listening for TCP connections on port 8080.

## Client Usage

### Start the client

```bash
python CLI.py
```

### Commands

| Command | Description | Example |
| :--- | :--- | :--- |
| **`ADD <key> <value>`** | Inserts a new key-value pair or updates an existing one. | `ADD user username` |
| **`GET <key>`** | Retrieves the value associated with the given key. | `GET user` |
| **`DEL <key>`** | Marks a key as deleted in the log file. | `DEL user` |
| **`COMPACT`** | Performs log rotation to clean up stale or deleted data, generating a new active log file. | `COMPACT` |

## Future Improvements (TODO)

* Abstract the networking layer to support Linux/macOS
* Implement signal handling for graceful shutdown to safely drain the thread pool queue
* Replace manual compaction with a manual background worker thread
* Add a "Time to Live" option for keys
