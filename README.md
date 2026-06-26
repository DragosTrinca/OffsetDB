# OffsetDB

## Description

A lightweight, multi-threaded key-value database server build in C++17.
It uses an append-only log and in-memory offset indexing for fast disk loopups.

Instead of scanning the file or loading the entire database into RAM, OffsetDB jumps instantly to the exact byte on the disk to retrieve data.

## Prerequisites

* **Operating System:** Windows (relies on `winsock2.h` and `Ws2_32.lib`)
* **Compiler:** A C++17 compliant compiler (e.g., MSVC)
* **IDE:** Visual Studio (Recommended, uses standard `.sln` / `.vcxproj`)

## Run the Server

* Clone the repository
* Open the .sln file in Visual Studio.
* Build the solution (Release or Debug).
* Execute the compiled binary.
* The server will automatically load existing data from database_log.txt (or create it if it doesn't exist) and start listening for TCP connections on port 8080.

## Client Usage

You can interact with OffsetDB using any standard TCP client (like telnet, netcat, or a custom script) by connecting to 127.0.0.1:8080.

The server accepts raw text strings separated by spaces.

### Commands

* **ADD \<key> \<value>**
* **GET \<key>**
* **DEL \<key>**
