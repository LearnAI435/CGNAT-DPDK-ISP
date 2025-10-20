# DPDK CGNAT Architecture

## System Overview

The DPDK CGNAT is designed for production ISP deployments requiring line-rate packet processing at 10-100 Gbps with sub-10 microsecond latency.

## Core Architecture Principles

### 1. Lockless Design
- **Per-Core Data Structures**: Each worker core has its own NAT hash tables, port pools, and statistics
- **No Mutex Contention**: Zero locking in the critical packet processing path
- **RSS Flow Affinity**: NIC distributes flows to cores using Receive Side Scaling

### 2. Zero-Copy Packet Processing
- **Kernel Bypass**: DPDK eliminates kernel networking stack overhead
- **Direct NIC Access**: Packets flow from NIC → User space → NIC
- **Huge Pages**: 2MB/1GB pages reduce TLB misses by 512x

### 3. Batch Processing
- **Burst I/O**: Process 32-256 packets per burst instead of one-by-one
- **Cache Efficiency**: Amortize function call overhead across batches
- **Vectorization**: SIMD instructions for checksum calculation

## Data Flow

```
┌─────────────┐
│   NIC (RSS) │
└──────┬──────┘
       │ Distributes flows to queues based on hash
       │
   ┌───┴────────────────────────┐
   │                            │
┌──▼──────┐              ┌──────▼──┐
│ Queue 0 │              │ Queue 7 │
└──┬──────┘              └──────┬──┘
   │                            │
┌──▼─────────────┐     ┌────────▼────┐
│ Worker Core 0  │ ... │ Worker Core 7│
│                │     │              │
│ Lockless NAT   │     │ Lockless NAT │
│ Hash Table     │     │ Hash Table   │
│ Port Pool      │     │ Port Pool    │
└──┬─────────────┘     └────────┬────┘
   │                            │
   └───┬────────────────────────┘
       │
   ┌───▼────┐
   │  NIC   │
   └────────┘
```

## Component Architecture

### 1. DPDK Runtime Layer
- **EAL (Environment Abstraction Layer)**: DPDK initialization
- **Port Configuration**: NIC setup with RSS and multiple queues
- **Memory Pools**: Packet buffers and NAT entry allocators

### 2. NAT Engine
- **Hash Tables**: Per-core rte_hash for 5-tuple lookups
- **Port Allocator**: Bitmap-based with O(1) allocation
- **State Machine**: TCP/UDP connection tracking
- **Timer Wheels**: Efficient connection aging

### 3. Telemetry System
- **Prometheus Exporter**: HTTP server for metrics
- **Statistics Aggregator**: Collects per-core stats
- **Structured Logging**: JSON logs for ELK stack

### 4. Control Plane
- **REST API**: Configuration and monitoring
- **YAML Config**: ISP-specific settings
- **Hot Reload**: Update config without restart

## Performance Characteristics

### Throughput
- **Per Core**: ~10 Gbps (8-10 million packets/sec)
- **16 Cores**: 100+ Gbps
- **Scaling**: Linear with core count

### Latency
- **Average**: 5-10 microseconds
- **P99**: <20 microseconds
- **Jitter**: <5 microseconds

### Memory
- **Per Core**: ~200 MB (NAT tables + packet buffers)
- **16 Cores**: ~4 GB total
- **Huge Pages**: 2MB or 1GB pages

## Scalability Model

### Horizontal Scaling
- Add more worker cores for higher throughput
- Each core handles independent flows
- No coordination overhead

### NUMA Awareness
- Allocate memory on same socket as NIC
- Pin cores to NIC's NUMA node
- Avoid cross-socket traffic

## Comparison: Traditional vs DPDK

| Aspect | Traditional | DPDK CGNAT |
|--------|------------|------------|
| Packet Processing | Kernel (syscalls) | Userspace (zero-copy) |
| Thread Model | Shared with locks | Per-core lockless |
| Memory | Regular pages | Huge pages |
| I/O Model | One packet/syscall | Batch (32-256) |
| Throughput | ~1 Gbps/core | ~10 Gbps/core |
| Latency | 100+ μs | <10 μs |
| CPU Efficiency | Low | High |

## Reliability Features

### Watchdog
- Monitors worker core health
- Automatic restart on failure

### Graceful Degradation
- Continue with reduced capacity on core failure
- Port exhaustion alerts

### State Persistence
- Periodic NAT table checkpoints
- Fast recovery after restart

## Future Enhancements

1. **Hardware Offload**: Flow director, ACL, crypto
2. **IPv6 Support**: Dual-stack NAT64
3. **Advanced QoS**: Per-customer rate limiting
4. **ML-Based Optimization**: Predictive port allocation
5. **eBPF Integration**: Programmable packet classifier
