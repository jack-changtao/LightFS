# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

LightFS is a distributed S3-compatible object storage system written in C11. Targets 100-1000+ nodes, 10PB+, geo-distributed with async cross-DC replication (last-write-wins conflict resolution).

## Architecture

```
Client → Access Layer (HTTP/SigV4) → Gateway (EC/Placement/Routing) → Meta Server (sharded in-memory B+tree index)
                                                                        ↓
                                                              Storage Engine (per-disk blob store)
                                                              etcd (management plane only)
```

### Modules

| Module | Status | Location |
|--------|--------|----------|
| Storage Engine (Phase 1) | **Implemented** | `src/storage/`, `include/lightfs/bs*.h` |
| Access Layer (Phase 2) | **Implemented** | `src/access/`, `include/lightfs/access/` |
| Meta Server (Phase 3) | **Implemented** | `src/meta/`, `include/lightfs/meta/` |
| etcd Management (Phase 4) | Planned | `src/cluster/`, `include/lightfs/cluster/` |
| Gateway | Planned (not yet planned) | `src/gateway/`, `include/lightfs/gateway/` |
| Network (RPC) | **Implemented** | `rpc/` (separate git repo) |

### rpc/ Subproject

A separate git repo containing a binary RPC framing library built on SPDK thread model with pluggable transports (Mock, TCP, RDMA). See `rpc/CLAUDE.md` for details. This is the inter-module communication layer — Access, Gateway, and Meta Server use it for RPC calls.

### Key Design Decisions

- **Access + Gateway collocated** per node — eliminates one network hop
- **Metadata is a separate HA pool** — multiple meta servers, each owning a shard of the bucket index
- **Storage Engine is per-disk** — one instance per physical device, no filesystem (direct SPDK bdev I/O)
- **etcd is management plane only** — outage does not block reads/writes
- **Strong consistency within DC** for all operations; eventual consistency cross-DC
- **Bucket shard splitting**: one bucket starts on one meta server; splits at midpoint when index grows; parent blocks new splits while child is loading

## Build System

SPDK-based Makefiles. `SPDK_ROOT_DIR` environment variable defaults to `$(HOME)/spdk`.

```bash
# Top-level build (when implemented)
make

# Run all tests
make test

# Build specific module
make -C src/storage
make -C src/access
make -C src/meta
make -C src/cluster

# Run tests for specific module
cd test/storage && make run
cd test/access && make run
cd test/meta && make run
cd test/cluster && make run
```

### Dependencies

- **SPDK 26.01** — available at `/root/spdk` and system-wide at `/usr/include/spdk`
- **Criterion** — unit testing framework (needs installation: `dnf install criterion-devel` or `apt install libcriterion-dev`)
- **OpenSSL** — SigV4 HMAC-SHA256
- **libxml2** — S3 XML parsing/serialization
- **libcurl** — etcd HTTP client (v3 API via gRPC gateway)

## Specs and Plans

- Design spec: `docs/superpowers/specs/2026-05-05-lightfs-design.md`
- Implementation plans: `docs/superpowers/plans/2026-05-07-*.md`

Phase order: Storage Engine → Access Layer → Meta Server → etcd Management → Gateway

## Testing

All tests use **assert()** for verification. Each module has its own `test/` directory with standalone test binaries.

```bash
# Run all tests
make test

# Run specific module tests
cd test/access && make run
cd test/storage && make run
```

## Conventions

- **C11** language standard for all modules
- **Async callback-based API** — all blob store operations return immediately, results via callback
- **SPDK reactor thread model** — one thread per CPU core, owning disks/connections
- **No filesystem** — direct block I/O via SPDK bdev (Phase 1 uses in-memory stubs, disk I/O in Phase 2+)
- **Public headers** in `include/lightfs/` — only types from `bs_types.h` are exposed to other modules
- **Internal headers** in `src/*/` with `_internal.h` suffix — not exposed outside the module
