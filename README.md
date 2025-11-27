# Multithreaded UDP Chat Application

## Software Systems Assignment 2 - Preet Harquissandas and Mikhail Agakov
---

## Overview
This project implements a multithreaded, multi-client chat application using **UDP communication**, **POSIX threads**, synchronization primitives, and an **ncurses-based user interface**.

It is based on the requirements of the assignment covering:

- Internet communication with UDP
- Concurrency with multiple threads
- Synchronization of shared data structures
- Client–server architecture
- Proposed Extensions (PEs)

---

## 1. Architecture

### Client–Server Model
The application consists of:

- **One server** running on a known UDP port (`12000`)
- **Multiple clients**, each binding to a unique available UDP port

Communication is **two-way and message-based**. A client initiates requests, and the server responds or broadcasts messages as needed.


---

## 2. Communication Layer

The project uses UDP sockets via the wrapper functions provided in `udp.h`:

- `udp_socket_open`
- `udp_socket_read`
- `udp_socket_write`
- `set_socket_addr`

These wrappers simplify raw socket operations and abstract away low-level details. Each client and server bind to a UDP socket and exchange formatted request/response messages.

---

## 3. Client Implementation

### Port Binding
- Automatically binds to an available UDP port
- Ensures multiple clients can run on the same machine

### Thread Model
Each client spawns **two threads**:

**Sender Thread**
- Reads user input commands via ncurses
- Sends complete request strings to the server
- Request format: `<request_type>$<content>`

**Listener Thread**
- Listens for incoming UDP responses
- Displays messages immediately in a dedicated UI region
- Handles chat, admin messages, kicks, etc.

### Supported Request Types

| Request | Purpose | Example |
|---------|---------|---------|
| `conn$ name` | Connect to chat | `conn$ Alice` |
| `say$ msg` | Broadcast message | `say$ Hello all!` |
| `sayto$ recipient msg` | Private message | `sayto$ Bob Hi` |
| `mute$ name` | Mute a user | `mute$ Bob` |
| `unmute$ name` | Unmute a user | `unmute$ Bob` |
| `rename$ new_name` | Change username | `rename$ Alice123` |
| `disconn$` | Disconnect from server | `disconn$` |
| `kick$ name` | Admin removes a user (admin on port 6666) | `kick$ Bob` |

---

## 4. Ncurses-Based User Interface

The client uses **ncurses** to implement a split-window UI:

- **Top window:** live chat messages and system notifications
- **Bottom window:** user input bar

The UI updates in real-time, even while typing.

**Ncurses functions used:**
`initscr()`, `newwin()`, `wprintw()`, `wrefresh()`, `scrollok()`, `getstr()`

Non-blocking I/O is used for the listener thread, providing a clean, professional chat interface similar to modern text UIs.

---

## 5. Server Implementation

### Listener Thread
- Dedicated thread continuously reads UDP packets from any client
- Parses request types
- Spawns the appropriate handler thread

### Shared Linked List of Clients
The server maintains a thread-safe linked list holding:

- Client IP
- Port number
- Chat name
- Mute list
- Activity timestamp (PE2)

### Supported Server Actions

| Request | Server Behavior |
|---------|----------------|
| `conn$ name` | Add client, send confirmation |
| `say$ msg` | Broadcast to all clients |
| `sayto$ name msg` | Send to specific client only |
| `mute$ name` | Record mute preference |
| `unmute$ name` | Remove mute |
| `rename$ new` | Update name in shared list |
| `disconn$` | Remove client and confirm |
| `kick$ name` | Admin only; remove and notify |

Each request is serviced by a **dedicated thread**.

---

## 6. Synchronization

Since multiple threads access shared structures, the server uses a **reader-writer lock**:

**Reader threads:**
- Broadcasting messages
- Looking up recipients
- Checking mute lists

**Writer threads:**
- Adding/removing clients
- Renaming
- Updating mute lists
- Handling kicks

**Functions used:**
`pthread_rwlock_rdlock()`
`pthread_rwlock_wrlock()`
`pthread_rwlock_unlock()`

## 7. Proposed Extensions

### PE1: Message History on Connection
- Server maintains a circular buffer of the last 15 broadcast messages
- Protected by its own mutex or reader-writer lock
- Sent automatically to a client upon `conn$`

### PE2: Inactive Client Removal
- Server keeps `(last_active_time, client)` pairs in a thread-safe min-heap
- Monitoring thread:
  - Checks oldest entry
  - Sends `ping$`
  - Removes client if `ret-ping$` not received
- Ensures inactive users do not stay connected indefinitely

---

## 8. Build Instructions

Place the following files in your working directory:

- `chat_server.c`
- `chat_client.c`
- `udp.h`
- `udp.c`

**Compile**
```bash
gcc chat_server.c udp.c -o server -pthread
gcc chat_client.c udp.c -o client -pthread -l ncurses
./server
./client

