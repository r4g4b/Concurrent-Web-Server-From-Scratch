# Concurrent Web Server from Scratch

A fork-per-request HTTP/1.0 server written in C using raw POSIX sockets, serving static files with MIME detection, directory-traversal protection, and zombie-free concurrency handling.

## Overview

This project implements a working HTTP server entirely from OS primitives — no libraries beyond the standard POSIX socket API — to explore how processes, file I/O, sockets, and signal handling combine to build real infrastructure. It accepts `GET` requests and serves static files (HTML, images, etc.) from a user-specified directory.

## Features

- **TCP socket server**: `socket()` / `bind()` / `listen()` / `accept()` with `SO_REUSEADDR` to avoid restart failures.
- **Fork-per-request concurrency**: each connection is handed to a child process via `fork()`, so a crash or hang in one request can't affect another.
- **Zombie reaping**: a `SIGCHLD` handler using non-blocking `waitpid(-1, NULL, WNOHANG)` cleans up terminated children without the parent ever blocking on `wait()`.
- **HTTP/1.0 request parsing**: extracts method, URL, and protocol version; rejects non-`GET` methods with `405 Method Not Allowed`.
- **Directory traversal prevention**: rejects any URL containing `..` before touching the filesystem, returning `404 Not Found`.
- **MIME type detection**: infers `Content-Type` from file extension (`text/html`, `text/css`, `application/javascript`, `image/jpeg`, `image/png`, `image/gif`, `text/plain`, falling back to `application/octet-stream`).
- **File serving**: `open()` / `fstat()` / `read()` into a heap buffer, streamed back with `write()`.

## Build & Run

```bash
gcc -o server server.c
./server 8080 ./www
```

## Testing

```bash
# Serve an HTML file
curl http://localhost:8080/index.html

# Serve an image, inspecting headers
curl -v http://localhost:8080/logo.jpg -o /dev/null

# Missing file
curl http://localhost:8080/missing.html          # -> 404 Not Found

# Directory traversal attempt (blocked)
curl "http://localhost:8080/../etc/passwd"       # -> 404 Not Found

# Concurrent connections
curl http://localhost:8080/index.html &
curl http://localhost:8080/index.html &
curl http://localhost:8080/index.html &
wait
```

## Architecture Notes

### Fork-per-request accept loop
```c
while (1) {
    int client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) continue;

    pid_t pid = fork();
    if (pid == 0) {
        close(server_fd);
        handle_request(client_fd);
    } else {
        close(client_fd);
    }
}
```

### Zombie reaping
```c
void reap_children(int sig) {
    (void) sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}
```

## Design Trade-offs: fork() vs. threads vs. thread pool

| Model | Isolation | Overhead | Notes |
|---|---|---|---|
| `fork()` (used here) | Strong — separate address space per request | Highest — page tables, TLB flush per connection | A crash in one request can't corrupt others |
| `pthread_create()` | Weak — shared address space | Lower | Needs mutexes for any shared state |
| Thread pool | Weak — shared address space | Lowest (amortized) | Closest to production servers; most complex to implement correctly |

Under CPU load, the OS scheduler preempts child processes like any other task, so no single slow request blocks others — but N concurrent connections mean N full processes, each with its own page tables, which is where the fork model's memory cost shows up most.

## Key Concepts Demonstrated

| Concept | Where |
|---|---|
| TCP server socket lifecycle | socket setup |
| Process-based concurrency | `fork()` accept loop |
| Async-signal-safe cleanup | `SIGCHLD` handler |
| HTTP/1.0 protocol parsing | `handle_request()` |
| Path sanitization / security | `..` traversal check |
| File I/O via syscalls | `open`/`fstat`/`read`/`write` |

## Reference

Course project for CSAI 204: Operating Systems, Spring 2026 — Mini-Project 4.
