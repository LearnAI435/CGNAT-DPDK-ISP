# DPDK CGNAT Production Deployment Guide

## Table of Contents
1. [Hardware Requirements](#hardware-requirements)
2. [System Preparation](#system-preparation)
3. [Installation](#installation)
4. [Configuration](#configuration)
5. [Running the System](#running-the-system)
6. [Monitoring](#monitoring)
7. [Troubleshooting](#troubleshooting)
8. [High Availability](#high-availability)

## Hardware Requirements

### Minimum Specifications
- **CPU**: 8 cores (Intel Xeon E5/Xeon Scalable or AMD EPYC)
- **RAM**: 16 GB with huge page support
- **NIC**: DPDK-compatible 10GbE (Intel X710, Mellanox ConnectX-5)
- **Storage**: 50 GB SSD for logs
- **OS**: Ubuntu 22.04 LTS / RHEL 8+ / Debian 11+

### Recommended Specifications
- **CPU**: 16+ cores with hyper-threading disabled
- **RAM**: 32 GB (16 GB huge pages)
- **NIC**: Dual-port 25GbE/40GbE for redundancy
- **Storage**: 200 GB NVMe SSD
- **Network**: Direct fiber connection to core routers

### DPDK-Compatible NICs
- Intel: X710, XL710, XXV710, E810
- Mellanox: ConnectX-4/5/6
- Broadcom: NetXtreme-E
- Cisco: VIC 1400 series

## System Preparation

### 1. Install Required Packages

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install -y build-essential meson ninja-build python3-pyelftools
sudo apt install -y libnuma-dev libpcap-dev pkg-config
sudo apt install -y linux-headers-$(uname -r)
sudo apt install -y git curl wget
```

**RHEL/CentOS:**
```bash
sudo yum groupinstall -y "Development Tools"
sudo yum install -y meson ninja-build python3-pyelftools
sudo yum install -y numactl-devel libpcap-devel
sudo yum install -y kernel-devel
```

### 2. Install DPDK

```bash
# Download DPDK 23.11 LTS
cd /opt
sudo wget https://fast.dpdk.org/rel/dpdk-23.11.tar.xz
sudo tar xf dpdk-23.11.tar.xz
cd dpdk-23.11

# Build and install
meson setup build
ninja -C build
sudo ninja -C build install
sudo ldconfig
```

### 3. Configure Huge Pages

**For 2MB huge pages (recommended for most deployments):**
```bash
# Allocate 8192 pages (16 GB)
echo 8192 | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

# Mount huge pages
sudo mkdir -p /mnt/huge
sudo mount -t hugetlbfs nodev /mnt/huge

# Make permanent (add to /etc/fstab)
echo "nodev /mnt/huge hugetlbfs defaults 0 0" | sudo tee -a /etc/fstab

# Add to GRUB (in /etc/default/grub)
GRUB_CMDLINE_LINUX="default_hugepagesz=2M hugepagesz=2M hugepages=8192"
sudo update-grub
sudo reboot
```

**For 1GB huge pages (high-performance deployments):**
```bash
# Add to GRUB
GRUB_CMDLINE_LINUX="default_hugepagesz=1G hugepagesz=1G hugepages=16"
sudo update-grub
sudo reboot

# Verify
cat /proc/meminfo | grep Huge
```

### 4. Load VFIO Driver

```bash
# Load VFIO module
sudo modprobe vfio-pci

# Make permanent
echo "vfio-pci" | sudo tee -a /etc/modules

# Enable IOMMU in GRUB
# Add to GRUB_CMDLINE_LINUX in /etc/default/grub:
# intel_iommu=on iommu=pt (for Intel)
# amd_iommu=on iommu=pt (for AMD)
sudo update-grub
sudo reboot
```

### 5. Bind NIC to DPDK

```bash
# Find your NIC's PCIe address
lspci | grep Ethernet

# Example output: 02:00.0 Ethernet controller: Intel Corporation...

# Bind to DPDK (replace with your PCIe address)
cd /opt/dpdk-23.11
sudo ./usertools/dpdk-devbind.py --bind=vfio-pci 02:00.0

# Verify binding
./usertools/dpdk-devbind.py --status
```

## Installation

### 1. Build DPDK CGNAT

```bash
cd /opt
sudo git clone https://github.com/your-org/dpdk-cgnat.git
cd dpdk-cgnat

# Build
meson setup build
ninja -C build

# Install (optional)
sudo ninja -C build install
```

### 2. Configure System Limits

```bash
# Increase file descriptors
echo "* soft nofile 1000000" | sudo tee -a /etc/security/limits.conf
echo "* hard nofile 1000000" | sudo tee -a /etc/security/limits.conf

# Increase process limits
echo "kernel.pid_max = 4194304" | sudo tee -a /etc/sysctl.conf
sudo sysctl -p
```

## Configuration

### 1. Edit Configuration File

```bash
sudo cp config/cgnat.yaml /etc/dpdk-cgnat/cgnat.yaml
sudo nano /etc/dpdk-cgnat/cgnat.yaml
```

**Key settings to customize:**
```yaml
# Public IP pool (your actual ISP IPs)
nat:
  public_ips:
    - "YOUR.PUBLIC.IP.1"
    - "YOUR.PUBLIC.IP.2"
    # ... up to 10 IPs

# Customer IP ranges
  customer_ranges:
    - "10.0.0.0/16"  # Adjust to your customer network

# Core allocation
dpdk:
  cores: "0-7"  # Adjust based on your CPU
  queues: 8     # Match number of cores
```

### 2. Validate Configuration

```bash
# Validate YAML syntax
python3 -c "import yaml; yaml.safe_load(open('/etc/dpdk-cgnat/cgnat.yaml'))"
```

## Running the System

### 1. Manual Start

```bash
cd /opt/dpdk-cgnat

# Run with 8 cores, 4 memory channels, port 0
sudo ./build/dpdk-cgnat -c 0xff -n 4 -- -p 0x1 -q 8
```

### 2. Systemd Service (Recommended)

```bash
# Create systemd service
sudo nano /etc/systemd/system/dpdk-cgnat.service
```

```ini
[Unit]
Description=DPDK CGNAT Service
After=network.target

[Service]
Type=simple
ExecStart=/opt/dpdk-cgnat/build/dpdk-cgnat -c 0xff -n 4 -- -p 0x1 -q 8
Restart=always
RestartSec=10
LimitNOFILE=1000000
LimitNPROC=1000000

[Install]
WantedBy=multi-user.target
```

```bash
# Enable and start service
sudo systemctl daemon-reload
sudo systemctl enable dpdk-cgnat
sudo systemctl start dpdk-cgnat

# Check status
sudo systemctl status dpdk-cgnat

# View logs
sudo journalctl -u dpdk-cgnat -f
```

## Monitoring

### 1. Prometheus Metrics

Access metrics at: `http://YOUR_SERVER_IP:9091/metrics`

```bash
# Example: Get current statistics
curl http://localhost:9091/metrics | grep cgnat_
```

### 2. Grafana Dashboard

```bash
# Install Grafana
sudo apt install -y grafana
sudo systemctl start grafana-server

# Access at http://YOUR_SERVER_IP:3000
# Default login: admin/admin
```

**Import dashboard:**
1. Go to Dashboards → Import
2. Use dashboard ID: 15000 (or create custom)
3. Configure Prometheus data source

### 3. Real-Time Console Logs

```bash
# View DPDK logs
sudo journalctl -u dpdk-cgnat -f --no-pager

# Monitor performance
watch -n 1 'curl -s http://localhost:9091/metrics | grep -E "packets|sessions|latency"'
```

## Troubleshooting

### Issue: DPDK fails to initialize

```bash
# Check huge pages
cat /proc/meminfo | grep Huge

# Check IOMMU
dmesg | grep -i iommu

# Verify VFIO driver
lsmod | grep vfio
```

### Issue: No packets received

```bash
# Verify NIC binding
/opt/dpdk-23.11/usertools/dpdk-devbind.py --status

# Check link status
ethtool eth0

# Verify RSS configuration
ethtool -x eth0
```

### Issue: High packet drops

```bash
# Increase RX ring size
# Tune NIC parameters with ethtool
sudo ethtool -G eth0 rx 4096 tx 4096

# Check CPU affinity
ps -eLo pid,tid,psr,comm | grep dpdk-cgnat

# Monitor core utilization
mpstat -P ALL 1
```

### Issue: Port exhaustion

```bash
# Check current utilization
curl -s http://localhost:9091/metrics | grep port

# Increase public IPs in config
# Or implement port recycling tuning
```

## High Availability

### 1. Active-Standby Setup

```bash
# Install Keepalived on both nodes
sudo apt install keepalived

# Configure VRRP (Virtual Router Redundancy Protocol)
sudo nano /etc/keepalived/keepalived.conf
```

### 2. State Synchronization

For production HA, implement NAT state sync between nodes using:
- Custom state replication protocol
- Redis/Memcached for shared state
- DPDK shared memory regions

### 3. Failover Testing

```bash
# Simulate failure on primary
sudo systemctl stop dpdk-cgnat

# Monitor failover time
ping -c 100 YOUR_VIRTUAL_IP
```

## Performance Tuning

### CPU Tuning
```bash
# Disable CPU frequency scaling
sudo cpupower frequency-set -g performance

# Disable hyper-threading (in BIOS)

# Isolate worker cores
# Add to GRUB: isolcpus=1-7
```

### NIC Tuning
```bash
# Increase interrupt coalescing
sudo ethtool -C eth0 rx-usecs 100

# Enable flow control
sudo ethtool -A eth0 rx on tx on
```

### NUMA Optimization
```bash
# Check NUMA topology
numactl --hardware

# Pin memory and cores to same NUMA node
numactl --cpunodebind=0 --membind=0 ./dpdk-cgnat ...
```

## Security Hardening

1. **Run as non-root with capabilities**
2. **Enable SELinux/AppArmor**
3. **Firewall DPDK traffic**
4. **Encrypt management interfaces**
5. **Regular security updates**

## Support

For production deployments, contact: ISP-support@example.com
