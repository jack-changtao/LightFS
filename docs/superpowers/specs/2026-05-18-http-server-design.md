# LightFS HTTP Server Design Spec

**Date:** 2026-05-18  
**Status:** Draft  
**Decision:** SPDK sock + llhttp (Option 1)

## Motivation

The LightFS Access Layer currently has no real HTTP server — it `printf()`s responses to stdout. The design spec calls for a high-performance, non-blocking HTTP server that integrates with SPDK's reactor threading model. After evaluating three approaches, we chose to build on SPDK's `sock` library with llhttp as the HTTP parser.

## Approach Selection

| Option | Verdict |
|--------|---------|
| SPDK sock + llhttp (custom) | **Chosen** — full control, SPDK-native, no external event loop |
| libevhtp / Seastar HTTP | Rejected — external event loop conflicts with SPDK reactor |
| Separate HTTP sidecar (nginx) | Rejected — extra network hop violates collocated Access+Gateway design |

## Architecture

```
Client (HTTP/1.1, keep-alive, chunked, TLS)
       │
       ▼
┌──────────────────────────────────────────────┐
│          SPDK Reactor Thread (per core)       │
│                                              │
│  spdk_sock (listen/accept/read/write)        │
│       │                                      │
│  ┌────▼─────────────────────────────────┐   │
│  │  http_parser (llhttp)                │   │
│  │  callbacks → s3_request_t            │   │
│  └────┬─────────────────────────────────┘   │
│       │                                      │
│  ┌────▼─────────────────────────────────┐   │
│  │  现有管线: route → auth → handler     │   │
│  └────┬─────────────────────────────────┘   │
│       │                                      │
│  ┌────▼─────────────────────────────────┐   │
│  │  http_response → iovec[]             │   │
│  │  spdk_sock_writev_async()            │   │
│  └──────────────────────────────────────┘   │
│                                              │
│  Gateway (function call, zero network hop)   │
└──────────────────────────────────────────────┘
```

Key principles:
- No external event loop — everything runs on SPDK reactor ticks
- Access + Gateway collocated per core via direct function call
- llhttp vendored as single-file C (no runtime dependency)
- Zero-copy I/O via iovec wherever possible

## Component Design

### 1. HTTP Parser Integration (llhttp)

llhttp is the C HTTP parser used by Node.js, generated from TypeScript via llparse for state-machine correctness verification. It is callback-driven, allocates no memory, and performs no I/O.

**Integration model:**

```
spdk_sock_recv → byte buffer
       │
       ▼
llhttp_execute(parser, buffer, len)
       │
       ├── on_url(url, len)               → request.uri
       ├── on_header_field(name, len)      → cache header name
       ├── on_header_value(val, len)       → map to request fields
       ├── on_body(data, len)              → append to request.body
       ├── on_headers_complete()           → method, version, keep-alive flag
       └── on_message_complete()           → mark complete → dispatch
```

**Chunked encoding:** llhttp transparently decodes chunked transfer encoding. Multiple `on_body` callbacks are invoked, each with a single decoded chunk. The caller sees a continuous data stream.

**Keep-alive detection:** `llhttp_should_keep_alive()` per RFC 7230:
- HTTP/1.1 → keep-alive by default
- HTTP/1.0 → keep-alive only with explicit `Connection: keep-alive`
- `Connection: close` → force close regardless of version

**Error codes → HTTP status:**

| llhttp Error | HTTP Status |
|-------------|-------------|
| `HPE_INVALID_METHOD` | 400 Bad Request |
| `HPE_INVALID_URL` | 400 Bad Request |
| `HPE_INVALID_HEADER_TOKEN` | 400 Bad Request |
| `HPE_INVALID_CHUNK_SIZE` | 400 Bad Request |
| `HPE_USER` (body too large) | 413 Payload Too Large |

**Core struct:**

```c
typedef struct http_parser_ctx {
    llhttp_t parser;
    llhttp_settings_t settings;
    s3_request_t *request;
    bool headers_complete;
    bool message_complete;
    bool keep_alive;
    char *body_buf;
    uint32_t body_len;
    uint32_t body_capacity;
} http_parser_ctx_t;
```

### 2. Connection Management

**Per-connection state machine:**

```
IDLE ──(readable)──→ READING ──(complete)──→ HANDLING
  ↑                                              │
  │                                              ▼
  └──── keep_alive=true ◄── WRITING ◄────────────┘
  │
  └──── keep_alive=false → CLOSE → DESTROYED
```

- IDLE → reading next request on a keep-alive connection
- READING → accumulating bytes, feeding llhttp
- HANDLING → route → auth → handler (existing code)
- WRITING → `spdk_sock_writev_async` pending
- After write completes: keep-alive → IDLE, otherwise → CLOSE

**Core structs:**

```c
typedef struct http_conn {
    struct spdk_sock       *sock;
    http_parser_ctx_t      parser_ctx;
    uint8_t                *recv_buf;
    uint32_t               recv_buf_size;
    uint32_t               recv_len;
    struct spdk_sock_group *group;
    struct http_server     *server;
    TAILQ_ENTRY(http_conn) link;
    uint64_t               last_active_ts;
} http_conn_t;

typedef struct http_server {
    struct spdk_sock   *listen_sock;
    struct spdk_sock_group *group;
    TAILQ_HEAD(, http_conn) conn_list;
    uint32_t           conn_count;
    uint32_t           max_connections;
    uint32_t           keep_alive_timeout_ms;   // default 5000
    uint32_t           request_timeout_ms;       // default 30000
} http_server_t;
```

**Limits and eviction:**
- `max_connections` — exceed → accept then immediately close, return 503
- `keep_alive_timeout_ms` — idle connections closed after timeout
- `request_timeout_ms` — incomplete reads closed after timeout
- Timeout check runs once per second in a SPDK poller, scanning the connection list

**Buffer strategy:**
- Pre-allocated buffer pool via `spdk_sock_group_provide_buf()`
- Default 64 KB recv buffer per connection
- Body extends up to `access_server_config_t.max_request_body`
- Leftover data after one request remains in buffer for the next (pipelining support)

### 3. HTTP Response Serialization

Convert `s3_response_t` to iovec array, zero-copy to `spdk_sock_writev_async()`.

```c
int http_response_serialize(const s3_response_t *response,
                            http_version_t http_version,
                            bool keep_alive,
                            struct iovec *iov, int max_iov);
```

**iovec layout (typical GET response):**

```
iov[0] = "HTTP/1.1 200 OK\r\n"           ← status line (static table)
iov[1] = "Content-Type: application/octet-stream\r\n"
iov[2] = "Content-Length: 1234\r\n"
iov[3] = "ETag: \"abc...\"\r\n"
iov[4] = "Connection: keep-alive\r\n"
iov[5] = "\r\n"                           ← empty line separator
iov[6] = response.body (pointer, no copy) ← payload
```

All static strings pre-defined as `const struct iovec`. Status lines from a static lookup table. No snprintf in the hot path.

**S3 operation → response mapping:**

| Operation | Status | Key Headers | Body |
|-----------|--------|-------------|------|
| GET Object | 200 | Content-Type, Content-Length, ETag, Last-Modified | object data |
| PUT Object | 200 | ETag | minimal XML |
| DELETE Object | 204 | — | none |
| List Objects | 200 | Content-Type: application/xml | XML |
| HEAD Object | 200 | Content-Type, Content-Length, ETag | none |
| Error | 400/403/404/500 | Content-Type: application/xml | XML error |

### 4. SPDK Reactor Integration

Single poller per reactor tick:

```
spdk_poller
   ├── spdk_sock_group_poll(group)
   │     ├── sock_A readable → recv → llhttp_execute
   │     ├── sock_B readable → recv → llhttp_execute (complete) → dispatch → writev_async
   │     └── sock_C write-done callback → keep-alive? → continue read or close
   ├── timeout check (once per second): evict idle/stuck connections
   └── connection limit enforcement
```

### 5. HTTPS / TLS

TLS is enabled through SPDK's sock layer SSL socket module. When TLS is configured:
- `spdk_sock_listen_ext()` with `impl_opts.tls_version` set
- Certificate and key configured via `spdk_sock_impl_opts`
- The sock layer handles TLS handshake transparently
- Application code sees plaintext — identical to non-TLS path

TLS configuration is a startup-time setting in `http_server_config_t`. TLS support is optional — if no certificate is provided, the server runs plain HTTP.

## File Structure

```
src/access/
├── http_server.c          # Connection management + spdk_sock integration
├── http_parser.c          # llhttp wrapper + callbacks
├── http_response.c        # Response serialization
├── access_server.c        # Modified: call http_server instead of printf
├── access_routes.c        # Unchanged
├── access_handlers.c      # Unchanged
├── s3_xml.c               # Unchanged
├── sigv4.c                # Unchanged
├── third_party/
│   ├── llhttp.h           # Vendored from github.com/nodejs/llhttp (release v9.3.x)
│   └── llhttp.c           # Vendored single-file C
└── Makefile

include/lightfs/access/
├── http_server.h          # http_server_config_t, http_server_start/stop
├── access_server.h        # Interface unchanged
├── access_types.h         # May add streaming body fields
├── s3_xml.h               # Unchanged
└── sigv4.h                # Unchanged

test/access/
├── test_http_parser.c     # llhttp callback correctness
├── test_http_response.c   # Response serialization
├── test_conn_state.c      # Connection state machine (mock spdk_sock)
├── test_http_integration.c # End-to-end via real TCP socket
└── Makefile
```

## Public API

```c
// http_server.h
typedef struct {
    const char *listen_host;       // "0.0.0.0" or specific IP
    uint16_t    listen_port;       // 80 or 443
    uint32_t    max_connections;   // 0 = default (1024)
    uint32_t    keep_alive_timeout_ms;  // 0 = default (5000)
    uint32_t    request_timeout_ms;     // 0 = default (30000)
    uint32_t    max_request_body;  // 0 = default (64MB)
    const char *tls_cert_path;     // NULL = no TLS
    const char *tls_key_path;      // NULL = no TLS
} http_server_config_t;

int  http_server_start(const http_server_config_t *config);
void http_server_stop(void);
```

## Testing Strategy

Four test layers, all using `assert()`:

| Layer | What | Mock/Real |
|-------|------|-----------|
| llhttp callbacks | Feed bytes → verify `s3_request_t` fields | No mock needed (pure parser) |
| Response serialization | `s3_response_t` → verify iovec content | No mock needed (pure function) |
| Connection state machine | State transitions, keep-alive cycle | Mock `spdk_sock` |
| Integration | Full request/response via TCP | Real socket |

Test coverage matrix for the parser:
- GET/PUT/DELETE/HEAD with headers
- Chunked transfer encoding (multiple on_body)
- Keep-alive detection (HTTP/1.1 vs 1.0 vs Connection: close)
- Invalid requests (bad method, oversized header, etc.)
- Multiple pipelined requests on one connection
- Empty body, large body

## Dependencies

| Dependency | Purpose | License |
|-----------|---------|---------|
| SPDK (`sock`, `event`) | Async socket I/O, reactor | BSD-3-Clause |
| llhttp (vendored) | HTTP/1.1 parsing | MIT |
| OpenSSL | TLS (via SPDK sock SSL module) | Apache-2.0 |

## Integration with Existing Code

The existing Access Layer pipeline is preserved unchanged:

```c
// http_parser.c — callback on_message_complete
void on_request_complete(http_conn_t *conn) {
    s3_request_t *request = conn->parser_ctx.request;

    // Existing code, unchanged:
    int result = s3_route_parse(method, uri, headers, header_count, request);
    if (result != 0) { /* 400 */ return; }

    sigv4_result_t auth = sigv4_validate(...);
    if (auth != SIGV4_ERROR_OK) { /* 403 */ return; }

    s3_response_t response = {0};
    s3_handler_put/get/delete/list(request, &response);

    http_response_serialize(&response, ...);
    spdk_sock_writev_async(...);
}
```

## Open Items

- TLS certificate/key loading from file (startup-time, not hot path)
- Request body streaming for very large PUTs (pass data to handler incrementally rather than buffering entire body — deferred to Phase 2)
- HTTP pipelining fully supported by the llhttp/connection model but not explicitly tested until large-scale benchmarking
- SPDK `sock` impl selection: default "posix" for now, "uring" for production optimization later
