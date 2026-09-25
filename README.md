# http-server

A multithreaded HTTP/1.0 file server written in C with POSIX sockets and
pthreads. It serves static files from a directory using a fixed pool of worker
threads that pull client connections from a bounded, thread-safe queue.

## Features

- **Thread pool:** 5 worker threads consume connections from a bounded
  producer/consumer queue built on a mutex and condition variables.
- **Static file serving:** `GET` requests for `.txt`, `.html`, `.jpg`, `.png`,
  and `.pdf` files are served with the matching `Content-Type`. Other files are
  sent as `application/octet-stream`.
- **Path traversal protection:** Request paths are resolved with `realpath()`
  and rejected with `403` if they resolve outside the served directory.
- **Robust I/O:** Requests are read until the end of the headers (8 KB limit)
  and short writes are retried. `SIGPIPE` is ignored so a client disconnecting
  mid-response only fails that write.
- **Correct status codes:** Every error response has a `Content-Type` and a
  matching `Content-Length`.
- **Clean shutdown:** `SIGINT` stops accepting connections, wakes the blocked
  worker threads, and joins them before exiting.

| Status | When |
|--------|------|
| `200 OK` | The file exists and was sent |
| `400 Bad Request` | Malformed request line, non-HTTP/1.x version, or headers over 8 KB |
| `403 Forbidden` | The path resolves outside the served directory, or the file isn't readable |
| `404 Not Found` | The file doesn't exist or isn't a regular file |
| `405 Method Not Allowed` | Any method other than `GET` (includes `Allow: GET`) |
| `500 Internal Server Error` | Unexpected server-side failure |

## Build

Requirements: a C compiler (`gcc` or `clang`) and `make`. Builds on Linux and
macOS.

```sh
make            # produces ./http_server
make sanitize   # produces ./http_server_asan and ./http_server_tsan
make clean      # removes build artifacts
```

The build uses `-Wall -Wextra -Werror`, so any compiler warning fails the build.

`make sanitize` builds two instrumented binaries, since AddressSanitizer and
ThreadSanitizer can't be combined:

- `http_server_asan` runs with AddressSanitizer and UndefinedBehaviorSanitizer
  to catch memory errors and undefined behavior.
- `http_server_tsan` runs with ThreadSanitizer to catch data races between the
  accept loop and the worker threads.

## Usage

```
./http_server DIRECTORY PORT
```

### Example session

```sh
$ ./http_server server_files 8000 &

$ curl -i localhost:8000/quote.txt                        # serve a file
HTTP/1.0 200 OK
Content-Type: text/plain
Content-Length: ...

$ curl -i localhost:8000/missing.txt                      # missing file
HTTP/1.0 404 Not Found

$ curl -i -X POST localhost:8000/quote.txt                # wrong method
HTTP/1.0 405 Method Not Allowed
Allow: GET

$ curl -i --path-as-is localhost:8000/../../etc/passwd    # traversal attempt
HTTP/1.0 403 Forbidden

$ kill -INT %1                                            # clean shutdown
```

## How it works

```
                    ┌──────────────────────────┐      ┌──────────┐
 clients ──accept──▶│ connection queue (cap 5) │─────▶│ worker 1 │
   (main thread)    │  mutex + 2 cond vars     │─────▶│   ...    │
                    └──────────────────────────┘─────▶│ worker 5 │
                                                      └──────────┘
```

- **Accept loop** (`main`): The main thread accepts connections and enqueues
  their file descriptors. `connection_enqueue` blocks while the queue is full.
- **Workers** (`consumer_loop`, `handle_client`): Each worker dequeues a
  connection, parses the request, resolves the path, writes the response, and
  closes the socket. A failure on one connection is logged and the worker moves
  on to the next.
- **Request parsing** (`read_http_request`): The server reads until it sees
  `\r\n\r\n`, then parses the request line with bounded `sscanf` widths. It
  returns an HTTP status instead of failing outright, so the caller can send the
  right error.
- **Path resolution** (`resolve_resource_path`): The requested path is joined to
  the served directory with `snprintf` and a length check, canonicalized with
  `realpath()`, and compared against the served directory, which is resolved
  once at startup.
- **Response** (`write_http_response`): The server opens the file, uses `fstat`
  to confirm it's a regular file and get its size, sends the headers, and
  streams the contents in 512-byte chunks through `write_all`, which retries
  short writes.
- **Signals:** Worker threads are created with all signals blocked, so `SIGINT`
  is delivered to the main thread. There it interrupts `accept`, and
  `connection_queue_shutdown` wakes every blocked worker so they can be joined.

## Project structure

```
http_server.c          Entry point: socket setup, accept loop, worker threads, path resolution
http.c/.h              Request parsing and response writing
connection_queue.c/.h  Bounded thread-safe queue of client file descriptors
concurrent_open.c      LD_PRELOAD shim used by the tests to verify concurrency (Linux only)
Makefile               Build, sanitizer, and test targets
server_files/          Sample files to serve
testius                Test runner by John Kolb (Python 3, GPL-3.0)
test_cases/            Test definitions and expected output
```

## Testing

The concurrency test starts the server with `concurrent_open.so` preloaded.
This shim intercepts `open()` and blocks each call until 5 threads are opening
files at the same time. A server that doesn't really handle requests in
parallel therefore hangs. The test fetches every file in `server_files/`
concurrently with `curl`, shuts the server down with `SIGINT`, and diffs each
download against the original.

Requirements: Linux, Python 3.7+, and `curl`.

```sh
make test              # run the suite (port 8000 by default)
make test port=9000    # use a different port
make clean-tests       # remove test output
```

For race and memory checking, run the same kind of traffic against the
sanitizer builds from `make sanitize`.

Tests are run with [`testius`](testius), a test runner by John Kolb
(GPL-3.0-or-later).
