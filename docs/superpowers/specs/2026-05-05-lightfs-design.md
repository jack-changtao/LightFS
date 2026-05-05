# LightFS — Distributed S3-Compatible Object Storage Design

## Overview

LightFS is a distributed object storage system implementing the AWS S3 standard interface. It targets large-scale (100-1000+ nodes, 10PB+), geo-distributed deployments with eventually-consistent cross-DC replication.

**Language:** C (all modules).
**Build system:** SPDK-based, referencing the existing `rpc/` framework.

### Use Cases

- High-throughput data analytics (large sequential I/O)
- General-purpose S3-compatible storage (broad API, multi-tenancy)
- Low-latency object serving (NVMe + RDMA)
- On-premises private cloud storage

### API Scope (Phase 1)

Core S3 operations + multipart upload, versioning, lifecycle policies, object locking, SSE-S3/SSE-C, SigV4 auth, presigned URLs.

---

## Architecture

### System Components

```
                         ┌─────────────────────────────────────┐
                         │              etcd                   │
                         │  topology / config / discovery /     │
                         │  meta checkpoints / shard map        │
                         └──┬──────────────────────────────┬───┘
                            │                              │
                            ▼                              ▼
 ┌──────────┐   S3 HTTP   ┌──────────────┐   ┌──────────────────────┐
 │  Client  │────────────►│ Access Layer │   │   Meta Server        │
 │  (SDK)   │             │ ──────────── │   │   ────────────────   │
 │          │             │ · SigV4 auth │   │   · Multiple shards   │
 │          │             │ · XML        │   │   · CoW B+tree in-mem │
 │          │             │ · SSE        │   │   · Checkpoint to SE  │
 │          │             └──────┬───────┘   │   · Recovery from SE  │
 └──────────┘                   │           │   · Background recovery│
            ┌───────────────────┼───────────┴───────────────────────┐
            │                   ▼                                   │
            │          ┌───────────────┐                             │
            │          │   Gateway     │◄── manifest push/query ────│
            │          │  ─────────── │                             │
            │          │  · EC encode │                             │
            │          │  · Placement │  RDMA / TCP                 │
            │          │  · Routing   │                             │
            │          │  · LRU cache │──────────────┐              │
            │          └──────────────┘              │              │
            │                                        ▼              │
            │                               ┌──────────────────┐    │
            │                               │  Storage Engine  │    │
 ─ ─ ─ ─ ─ ─│─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─│  ─────────────── │─ ─ ─│─ ─
   Other DC │                               │  · Raw disk via   │    │
   (async)  │                               │    io_uring       │    │
            │                               │  · Appen-only     │    │
            │                               │    segments       │    │
            │                               │  · NVMe/SSD/HDD   │    │
            │                               │  · GC             │    │
            │                               └──────────────────┘    │
            └───────────────────────────────────────────────────────┘
```

- **Access + Gateway collocated** per node — eliminates one network hop.
- **Metadata is a separate HA pool** — multiple meta servers, each owning a shard of the bucket index.
- **Storage Engine is per-disk** — one instance per physical device.
- **etcd is external** — management plane, not data plane. Outage does not block reads/writes.

---

## Module 1: Access Layer — S3 Protocol

Stateless HTTP frontend implementing the AWS S3 REST API.

### Request Flow

```
Client → Access Layer:
  1. Parse SigV4 auth header, validate
  2. Parse XML body (ACL, lifecycle config, etc.)
  3. SSE encrypt body (if enabled) — before handing to Gateway
  4. Forward to Gateway via RPC
  5. Return response to client
```

### Design Points

- **Stateless** — any access node serves any request, no sticky sessions.
- **Streaming** — large Put/Get bodies streamed, not fully buffered.
- **Auth** — pluggable middleware (SigV4 initially, STS later).
- **SSE** — encryption in Access layer before EC encoding; fragments are encrypted on disk.
- **HTTP framework** — high-performance C HTTP library, non-blocking I/O.

---

## Module 2: Gateway — EC, Routing, Replication

The core data path: EC encode/decode, fragment placement, replication, and manifest push to Meta Server.

Gateways are **stateless and load-balanced** — any gateway can serve any object.

### Gateway Components

| Component | Responsibility |
|-----------|---------------|
| **EC Engine** | Encode/decode using Reed-Solomon. Streaming: encode stripes as data arrives. |
| **Placement Engine** | Min-cost flow solver across failure domains (DC→Rack→Host→Disk). Picks targets for each fragment. |
| **Fragment Router** | Dispatches fragments via RDMA or TCP. Retries on write failure on alternative targets. |
| **Replication Engine** | Local: 2x or 3x full copies to distinct racks. Cross-DC: async replication after local commit. |
| **Manifest LRU Cache** | Read-side LRU cache of ObjectManifests from Meta Server. Populated on cache miss. |

### Adaptive Erasure Coding

| Signal | Small (<4MB) | Medium (4MB-64MB) | Large (>64MB) |
|--------|-------------|-------------------|---------------|
| **EC Policy** | 2x or 3x replication | 6+3 (1.5x overhead) | 10+4 (1.4x overhead) |
| **Stripe Size** | N/A (full copy) | 512KB | 4MB |
| **Storage Tier** | NVMe/SSD | SSD + HDD | HDD (cost-optimized) |

Bucket-level overrides take precedence. Object-level header overrides everything.

### Fragment Placement

```
Placement domain tree: DC → Rack → Host → Disk

Algorithm: min-cost flow
  1. Exclude same host (2 fragments on one machine = wasted durability)
  2. Span at least 2 racks (rack-level fault tolerance)
  3. Cap 2 fragments per rack
  4. Prefer disks with most free space

Cross-DC: for geo-replicated buckets, place at least 2 fragments in peer DC.
```

### Write Path

```
PutObject(bucket, key, data):
  1. EC Engine: select EC policy based on object size + bucket config
  2. Placement: pick K+M target nodes across failure domains
  3. If EC (medium/large):
       Stream in stripe_size chunks, encode → K+M fragments
       Fan-out write to all targets. ALL must succeed.
       On failure → retry on alternative node/disk.
     If replication (small):
       Pick R nodes across distinct racks (R=2 or R=3)
       Fan-out full object. ALL must succeed.
       On failure → retry on alternative node/disk.
  4. Build ObjectManifest, add to batch buffer
  5. Send PushManifestBatch to Meta Server, wait for batch ack
  6. Return 200 OK to client
  7. [Async] Cross-DC bucket: enqueue replication to peer DC
```

### Read Path

```
GetObject(bucket, key):
  1. Check Manifest LRU Cache → hit: use cached
  2. Miss: query Meta Server → get ObjectManifest
  3. If EC: read from K fastest of K+M nodes
     If replica: read from fastest replica
  4. Stream decoded data back (EC: decode stripe by stripe; replica: direct)
```

---

## Module 3: Storage Engine — Log-Structured Blob Store

Raw disk access via io_uring, SPDK reactor thread model, append-only segments, CoW B+tree index, GC.

### Architecture

- **No filesystem** — direct block I/O via io_uring (IORING_SETUP_IOPOLL).
- **Reactor thread model** — one reactor thread per CPU core, owning one or more disks.
- **io_uring ring per disk** — polling mode, no kernel interrupts.

### Segment Model

The disk is divided into fixed-size segments (e.g. 256MB). All writes are sequential append.

| Segment Type | Purpose | Contents |
|-------------|---------|----------|
| **Journal Segment** | Write-ahead log, durability boundary | Put/Delete/Seal records |
| **Data Segment** | Blob payload storage (max 4MB per blob) | Raw blob bytes |
| **Meta Segment** | CoW B+tree pages — blob index | Internal nodes + leaf pages + superblock |

### Object → Fragment → Blob Hierarchy

```
S3 Object (any size)
  → EC encode → K+M fragments
    → Each fragment split into N fixed-size blobs (max 4MB)
      → Each blob stored as: {segment_id, offset, size, crc}
        → CoW B+tree: blob_id → blob location
```

### CoW B+tree Index

- Key: blob_id → Value: {segment_id, offset, size, crc, state}
- Updates: copy modified path to new Meta Segment pages, update superblock with root pointer.
- Old pages become garbage → reclaimed by GC.

### Segment Lifecycle & GC

```
FREE → ACTIVE (accepting writes) → SEALED (full) → CLEANING (GC) → FREE

GC: select victim segment with lowest liveness (<20%), copy live blobs
to new segment, update CoW B+tree, mark old segment FREE.
```

### Interface (C API)

```c
typedef struct {
    uint64_t segment_id;
    uint64_t offset;
    uint32_t size;
    uint32_t crc;
} blob_location_t;

int bs_init(const char *disk_path, bs_config_t *cfg);
void bs_destroy(void);

int bs_put_blob(uint64_t blob_id, const void *data, uint32_t size,
                void (*cb)(int rc, blob_location_t *loc, void *arg), void *arg);
int bs_get_blob(blob_location_t *loc,
                void (*cb)(int rc, void *data, uint32_t size, void *arg), void *arg);
int bs_delete_blob(uint64_t blob_id,
                   void (*cb)(int rc, void *arg), void *arg);
int bs_stat_blob(uint64_t blob_id, int *state_out);
```

---

## Module 4: Metadata — Sharded, Blob-Backed Object Index

Multiple meta servers, each owns a shard of the bucket index. In-memory CoW B+tree, checkpoints to Meta Blobs, recovery from Storage Engines.

### Three-Tier Storage

| Tier | What | Role |
|------|------|------|
| **Memory** | CoW B+tree, Bucket Registry, Multipart State, Lifecycle Rules | Hot path — all reads/writes from memory |
| **Storage Engine** | Meta Blobs (serialized B+tree pages), Journal Blobs | Durability — periodic checkpoint, 2x/3x replication |
| **etcd** | Checkpoint pointers, shard map, bucket→shard mapping | Bootstrap + service discovery |

### Sharding

- Shard key: `hash(bucket_id) % num_shards → meta server`.
- Large buckets can be further split by key prefix hash.
- etcd: `/lightfs/meta/shards/{shard_id} → {owner, status, buckets[]}`

### Write Path (synchronous manifest push)

```
PutObject (meta server side):
  1. Gateway sends PushManifestBatch([m1..mN])
  2. Insert each manifest into in-memory CoW B+tree
  3. Mark affected pages dirty
  4. Return batch ack to Gateway

[Periodic checkpoint]:
  1. Serialize dirty B+tree pages → Meta Blobs in Storage Engine
  2. Write Checkpoint Blob
  3. Update etcd checkpoint pointer
```

Client latency = all data blob writes + batch wait + Meta Server RTT.
Meta Server acks from memory — no disk I/O per manifest.

### Read Path

- Gateway queries Meta Server → in-memory CoW B+tree lookup → return ObjectManifest.
- No disk I/O. Read-after-write consistency for GetObject/HeadObject.

### Crash Recovery

```
Meta Server restart:
  1. Read etcd: get checkpoint pointer {checkpoint_blob_id, root_page_blob_id, seq}
  2. Read checkpoint blob + all Meta Blob pages from Storage Engine
  3. Rebuild in-memory CoW B+tree (state at seq N)
  4. Query each Storage Engine: "list blobs with write_seq > N"
  5. Reconstruct/replay into B+tree → caught up to latest seq
  6. Update etcd: mark shard ACTIVE, flush fresh checkpoint
  7. Resume serving
```

### Background Recovery (Node/Disk Failure)

Triggered by etcd node/disk DOWN events. Meta Server coordinates:

1. Find all ObjectManifests referencing fragments on failed node/disk
2. For each affected object:
   - EC: read K surviving fragments → decode → write replacement fragment to healthy node
   - Replica: copy surviving replica to new node
3. Update ObjectManifest with new fragment locations
4. Prioritize objects with fewest remaining replicas

---

## Module 5: Network — RDMA + TCP (Deferred)

Will reference the existing `rpc/` framework (SPDK thread model, binary framing, pluggable TCP/RDMA transports).

---

## Module 6: Management — etcd-Based Cluster

### Node Lifecycle

- **Join**: register in etcd with lease (TTL 10s), report disks, advertise services, set ACTIVE.
- **Health**: etcd lease heartbeat. Lease expiry → node DOWN → Meta Server triggers recovery.
- **Leave**: drain requests, set DRAINING, revoke lease, shutdown.

### Configuration Management

| Config | Scope | How Applied |
|--------|-------|-------------|
| EC policies | Cluster default + per-bucket | Gateway watches etcd, reloads |
| Replication mode | Per-bucket (2x/3x) | Gateway reads bucket config |
| Storage tiers | Cluster-wide + per-disk | Storage Engine reads on startup |
| Lifecycle rules | Per-bucket | Meta Server evaluates |
| Meta shard map | Cluster-wide | Gateway + Access watch for routing |

### Service Discovery

All components watch relevant etcd prefixes to build local routing tables. etcd is the registry.

| Component | Watches | Builds |
|-----------|---------|--------|
| Access Layer | `/lightfs/cluster/discovery/gateways/` | Gateway list |
| Gateway | `/lightfs/cluster/discovery/storage_engines/`, `/lightfs/meta/shards/` | SE pool, Meta routing, topology |
| Storage Engine | (passive) | (none) |
| Meta Server | `/lightfs/cluster/topology/node/`, `/lightfs/cluster/topology/disk/` | Node/disk liveness → recovery |

---

## Consistency Model

| Operation | Consistency | Notes |
|-----------|-------------|-------|
| PutObject | Strong (per-object) | All data blobs + Meta Server memory ack before 200 |
| GetObject | Read-after-write | Immediate within same DC |
| ListObjects | Eventually consistent | May lag by one checkpoint interval |
| DeleteObject | Strong | Meta Server marks deleted in memory |
| Cross-DC reads | Eventual | Async replication lag |

---

## Data Integrity

- **Writes are all-or-nothing**: every fragment/replica must succeed before returning success.
- **No degraded reads in normal operation**: fragments are always available.
- **Recovery only on failure**: triggered by etcd node/disk DOWN events, coordinated by Meta Server.
- **Orphan blobs**: if Gateway crashes between writing data blobs and pushing manifest, blobs have no manifest reference. Background GC reclaims after grace period.
