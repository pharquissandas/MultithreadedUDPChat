# Multithreaded UDP Chat Application

## Software Systems Assignment 2
## Authors
* Preet Harquissandas (CID: 02589338)
* Mikhail Agakov (CID: 02560202)
---

## Overview
This project implements a multithreaded, multi-client chat application using **UDP communication**, **POSIX threads**, synchronization primitives, and an **ncurses-based user interface**.

It is based on the requirements of the assignment covering:

- Internet communication with UDP
- Concurrency with multiple threads
- Synchronization of shared data structures
- Client–server architecture
- Proposed Extensions (PEs)

## Summary

### Core Requirements
| Feature | Status | Description |
| :--- | :--- | :--- |
| **UDP Communication** | **Fully Implemented** | Uses custom wrappers (`udp.h`) for `sendto`/`recvfrom`. |
| **Multithreading** | **Fully Implemented** | **Client:** Sender & Listener threads. **Server:** Listener, Ping, & Worker threads. |
| **Synchronization** | **Fully Implemented** | Server uses `pthread_rwlock` for the client list. Client uses `pthread_mutex` for UI. |
| **Basic Chat** | **Fully Implemented** | Supports `conn$`, `say$`, and `disconn$` with broadcast. |
| **Private Messaging** | **Fully Implemented** | Supports `sayto$` for direct one-to-one messaging. |
| **Moderation** | **Fully Implemented** | Supports `mute$`, `unmute$`, and `kick$` (admin only). |
| **User Interface** | **Fully Implemented** | Ncurses split-window UI (Input vs. Chat history) with thread safety. |

### Proposed Extensions (PEs)
| Extension | Status | Description |
| :--- | :--- | :--- |
| **PE1: Message History** | **Fully Implemented** | Server maintains a circular buffer of the last 15 messages, sent to new users on join. |
| **PE2: Inactive Removal** | **Fully Implemented** | Background thread pings users inactive for 5 mins; kicks them if no response in 10s. |

### Extra Features
| Extension | Status | Description |
| :--- | :--- | :--- |
| **Unique Usernames** | **Fully Implemented** | Server rejects `conn$` or `rename$` if the requested name is already taken. |
| **Connection Enforcement** | **Fully Implemented** | Unconnected clients cannot send messages; they receive an error prompt. |
| **Target Validation** | **Fully Implemented** | `sayto`, `mute`, and `kick` verify the target exists before executing. |
| **Admin Security** | **Fully Implemented** | `kick$` command is strictly restricted to the client bound to port 6666. |

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
| `ret-ping$` | Resets last active timer | `ret-ping$` |

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
- Spawns a detached handler thread for every request to ensure concurrency.

### Ping Thread
- Runs in the background, waking up every 5 seconds.
- Checks if any client has been inactive for more than 5 minutes.
- If inactive: sends a `ping$` message telling the user to be active or be kicked.
- If the client does not respond with `ret-ping$` (or any other message) within 10 seconds, they are kicked.

### Shared Linked List of Clients
The server maintains a thread-safe linked list holding:

- Client IP & Port number
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
| `ret-ping` | Resets last active timer |

Each request is serviced by a **dedicated thread**.

---

## 6. Synchronization

Since multiple threads access shared structures (the client list and history buffer), the server uses a **reader-writer lock** (`pthread_rwlock_t`):

**Reader functions (Shared Access):**
- Looking up recipients (`sayto`)
- Checking mute lists during broadcast
- Reading history to send to new users

**Writer functions (Exclusive Access):**
- Connecting (`conn$`) or Disconnecting (`disconn$`)
- Renaming users
- Updating mute lists
- Handling kicks
- Updating the last 15-message circular buffer history
- Update last active timestamps
- Removing inactive clients (Ping thread)

**Functions used:**
`pthread_rwlock_rdlock()`
`pthread_rwlock_wrlock()`
`pthread_rwlock_unlock()`

---

## 7. Proposed Extensions

### PE1: Message History on Connection
- Server maintains a circular buffer of the last 15 broadcast messages
- Protected by its own mutex or reader-writer lock
- Sent automatically to a client upon `conn$`
- History is updated with every message and every server message
- If there are more than 15 messages the oldest one is replaced

### PE2: Inactive Client Removal
- Client node linked list is updated with last active time
- Ping thread:
  - Checks if the last active time of the client is over 5 minutes
  - If yes, sends `ping$` message : "PING - please type anything to stay connected"
  - Removes client if timer is not reset by any message or `ret-ping$` not received within 10 seconds
- Ensures inactive users do not stay connected indefinitely

---

## 8. Additional Features (Robustness & Error Handling)
Beyond the core assignment requirements, the application includes several validation checks to ensure stability and a better user experience:

* **Unique Username Enforcement:** The server checks the active client list and prevents new users from connecting (`conn$`) if the requested name is already taken.
* **Safe Renaming:** Users cannot rename (`rename$`) themselves to a username that currently belongs to another active user.
* **Target Validation:**
    * **Private Messages:** `sayto$` checks if the recipient exists before attempting delivery. If not, an error is returned to the sender.
    * **Muting:** `mute$` and `unmute$` verify that the target user exists before updating the local mute list.
    * **Kicking:** `kick$` verifies the target exists before attempting removal.
* **Connection Enforcement:** Clients cannot send broadcast messages (`say$`) until they have successfully established a named connection. Attempting to chat while unconnected prompts the user to connect first.
* **Admin Security:** The `kick$` command is strictly restricted to the admin client (bound to port 6666). Regular users attempting to kick others receive a "Permission Denied" response.

---

## 9. Build Instructions

Place the following files in your working directory:

- `chat_server.c`
- `chat_client.c`
- `udp.h`
- `udp.c`

**Compile**
```bash
gcc chat_server.c udp.c -o server -pthread
gcc chat_client.c udp.c -o client -pthread -l ncurses
```
**Run**
```bash
./server
./client
```

