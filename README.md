# DPDK-Based CGNAT for Production ISPs

## Overview
High-performance Carrier-Grade NAT (CGNAT) implementation using Intel DPDK for production ISP deployments. Designed to handle 20K-50K concurrent customers with only 10 public IP addresses at line rates of 10-100 Gbps.

## Key Features
- **Ultra-High Performance**: 10-100 Gbps throughput with <10μs latency
- **Kernel Bypass**: DPDK-based zero-copy packet processing
- **Lockless Architecture**: Per-core NAT tables eliminate contention
- **Scalable**: Linear scaling with CPU cores (tested up to 16 cores)
- **Production Ready**: Comprehensive monitoring, logging, and alerting
- **ISP Grade**: Built for 24/7 operation with failover and recovery

## Performance Targets
- **Throughput**: 50-200 million packets/sec
- **Latency**: <10 microseconds average
- **Capacity**: 50K concurrent NAT sessions
- **Ports**: 645K usable ports (10 IPs × 64,512 each)
- **CPU Efficiency**: ~10 Gbps per core

## Architecture

### Core Components
1. **DPDK Fastpath Workers** - Per-core packet processing pipelines
2. **Lockless NAT Engine** - Hash-based 5-tuple translation tables
3. **Port Pool Manager** - Bitmap-based port allocation per core
4. **Timer Wheels** - Efficient connection aging and cleanup
5. **Telemetry System** - Prometheus metrics and real-time stats
6. **REST API Server** - Control plane and monitoring interface
7. **React Dashboard** - Real-time web UI for operators
8. **Structured Logging** - High-speed per-core ring buffers

### Data Flow
```
NIC (RSS) → RX Queue → Worker Core → NAT Lookup/Update → TX Queue → NIC
     ↓
Per-Core Hash Table (Lockless)
     ↓
Statistics Aggregation → Telemetry → Dashboard
```

## Hardware Requirements
- **CPU**: 8-16 cores, Intel Xeon or AMD EPYC recommended
- **RAM**: 16-32 GB with huge page support
- **NIC**: DPDK-compatible (Intel X710, Mellanox ConnectX-5/6)
- **Network**: 10GbE or higher
- **OS**: Linux with huge pages enabled

## Quick Start

### 1. Install Dependencies
```bash
# Install DPDK (version 23.11 LTS recommended)
sudo apt install -y build-essential meson ninja-build python3-pyelftools
sudo apt install -y libnuma-dev libpcap-dev

# Install monitoring stack
sudo apt install -y prometheus grafana
```

### 2. Configure Huge Pages
```bash
# Allocate 2GB huge pages
echo 1024 | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
sudo mkdir -p /mnt/huge
sudo mount -t hugetlbfs nodev /mnt/huge
```

### 3. Build
```bash
cd dpdk-cgnat
meson setup build
ninja -C build
```

### 4. Run
```bash
# Bind NIC to DPDK driver
sudo ./scripts/dpdk-devbind.py --bind=vfio-pci 0000:02:00.0

# Run CGNAT (requires root for DPDK)
sudo ./build/dpdk-cgnat -c 0xff -n 4 -- -p 0x1 -C config/cgnat.yaml
```

### 5. Monitor
```bash
# Access dashboard
firefox http://localhost:8080

# View Prometheus metrics
curl http://localhost:9091/metrics
```

## Configuration

See `config/cgnat.yaml` for ISP-specific settings:
- Public IP pool
- Customer IP ranges
- Port allocation policies
- Timeout values
- Performance tuning

## Monitoring

### Real-Time Dashboard
- Active connections by customer
- Throughput (packets/sec, Gbps)
- Port pool utilization per IP
- Latency histograms
- Error rates and drops
- Per-core statistics

### Prometheus Metrics
- `cgnat_packets_processed_total`
- `cgnat_nat_sessions_active`
- `cgnat_port_pool_utilization`
- `cgnat_packet_latency_us`
- `cgnat_errors_total`

### Logging
- Structured JSON logs for ELK stack
- Per-customer connection tracking
- Port exhaustion alerts
- Performance anomalies

## Production Deployment

See `docs/DEPLOYMENT.md` for:
- High availability setup
- Redundancy configuration
- Backup and recovery
- Scaling guidelines
- Security hardening

## Development

```bash
# Run tests
ninja -C build test

# Enable debug logging
./build/dpdk-cgnat --log-level=debug

# Performance profiling
sudo perf record -g ./build/dpdk-cgnat
```

## License
MIT License - See LICENSE file

## Support
For ISP deployment support, contact: support@example.com
