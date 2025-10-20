# DPDK CGNAT Cloud Testing Guide

Complete guide for deploying and testing the DPDK CGNAT system in cloud environments.

---

## 🏆 Recommended: AWS EC2 (Best for Beginners)

AWS offers the most mature DPDK support with excellent documentation and automatic ENA driver setup.

### Why AWS?
- ✅ **All instances** (except T2) support DPDK with ENA driver
- ✅ **Pre-installed** ENA drivers on Amazon Linux 2, Ubuntu
- ✅ **Well-documented** setup process
- ✅ **Flexible pricing** - On-Demand, Spot instances available
- ✅ **Free tier eligible** for testing (c5.large)

---

## 📋 Quick Comparison: Cloud Providers

| Provider | NIC Driver | Setup Complexity | Cost (Est.) | Recommendation |
|----------|-----------|------------------|-------------|----------------|
| **AWS** | ENA | ⭐ Easy | $150-300/mo | **Best for testing** |
| **Azure** | Mellanox/MANA | ⭐⭐ Moderate | $200-400/mo | Production-ready |
| **GCP** | gVNIC | ⭐⭐⭐ Complex | $180-350/mo | Limited DPDK docs |

---

## 🚀 AWS EC2 Deployment (Step-by-Step)

### Phase 1: Choose Instance Type

#### For Testing (20K customers, 5-10 Gbps)
**Recommended: c5.4xlarge or c6i.4xlarge**
- **vCPUs**: 16 cores
- **Memory**: 32 GB RAM
- **Network**: Up to 10 Gbps
- **Cost**: ~$0.68/hour (~$490/month on-demand)
- **Spot Price**: ~$0.20-0.30/hour (~$150/month) 🎯

#### For Production Testing (50K customers, 25+ Gbps)
**Recommended: c5n.9xlarge or c6i.16xlarge**
- **vCPUs**: 36 cores
- **Memory**: 96 GB RAM
- **Network**: 50 Gbps (c5n) or 25 Gbps (c6i)
- **Cost**: ~$1.94/hour (~$1,400/month)
- **Spot Price**: ~$0.60/hour (~$430/month) 🎯

#### Budget Option (Learning Only)
**c5.2xlarge** - 8 vCPUs, 16GB RAM, 10 Gbps - $0.34/hour (~$245/month)

**💡 Pro Tip**: Use **Spot Instances** to save 60-80% on costs!

---

### Phase 2: Launch EC2 Instance

#### Step 1: AWS Console Setup

1. **Login to AWS Console** → Navigate to **EC2**
2. Click **Launch Instance**

**Instance Configuration:**
```
Name: dpdk-cgnat-test
AMI: Ubuntu Server 22.04 LTS (HVM), SSD Volume Type
     or Amazon Linux 2023
Instance Type: c5.4xlarge (or c6i.4xlarge)
Key Pair: Create new or use existing SSH key
```

**Network Settings:**
```
VPC: Default VPC (or your custom VPC)
Subnet: Any availability zone
Auto-assign Public IP: Enable
Security Group: Create new "dpdk-cgnat-sg"
  - SSH (22): 0.0.0.0/0 (or your IP only)
  - HTTP (8080): 0.0.0.0/0 (for dashboard)
  - Prometheus (9091): 0.0.0.0/0 (for metrics)
  - Custom ICMP/UDP: Allow for testing
```

**Additional Network Interfaces:**
- Click **Add network interface** (for DPDK data plane)
- Add **2-3 additional ENIs** (Elastic Network Interfaces)
  - **eth0**: Management/SSH (don't bind to DPDK!)
  - **eth1, eth2**: DPDK data plane

**Storage:**
```
Root Volume: 50 GB gp3 SSD
```

**Advanced Details:**
- Enable **Enhanced Networking**: Automatic with ENA
- Instance Metadata: IMDSv2 required

3. Click **Launch Instance**

#### Step 2: Connect to Instance

```bash
# SSH into your instance (replace with your key and IP)
ssh -i your-key.pem ubuntu@ec2-XX-XXX-XXX-XX.compute.amazonaws.com

# Or for Amazon Linux:
ssh -i your-key.pem ec2-user@ec2-XX-XXX-XXX-XX.compute.amazonaws.com
```

---

### Phase 3: Automated DPDK CGNAT Setup

I've created an **automated setup script** that installs everything in 5 minutes!

#### Quick Setup (One Command)

```bash
# Download and run automated setup
curl -fsSL https://raw.githubusercontent.com/LearnAI435/CGNAT-DPDK-ISP/main/scripts/aws-cloud-setup.sh | sudo bash
```

#### Manual Setup (If You Prefer Control)

**Step 1: Update System**
```bash
sudo apt-get update && sudo apt-get upgrade -y
sudo apt-get install -y build-essential git wget curl \
    libnuma-dev python3-pyelftools pkg-config \
    meson ninja-build libyaml-dev libmicrohttpd-dev
```

**Step 2: Install DPDK**
```bash
cd ~
wget https://fast.dpdk.org/rel/dpdk-23.11.tar.xz
tar xf dpdk-23.11.tar.xz
cd dpdk-23.11

# Build DPDK
meson build
cd build
ninja
sudo ninja install
sudo ldconfig
```

**Step 3: Configure Huge Pages**
```bash
# Allocate 8GB of 2MB huge pages
echo 4096 | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

# Mount huge pages
sudo mkdir -p /mnt/huge
sudo mount -t hugetlbfs nodev /mnt/huge

# Make persistent
echo "nodev /mnt/huge hugetlbfs defaults 0 0" | sudo tee -a /etc/fstab
```

**Step 4: Load DPDK Drivers**
```bash
# Load vfio-pci (recommended)
sudo modprobe vfio-pci

# Enable no-IOMMU mode (required for non-metal instances)
echo 1 | sudo tee /sys/module/vfio/parameters/enable_unsafe_noiommu_mode
```

**Step 5: Clone CGNAT Repository**
```bash
cd ~
git clone https://github.com/LearnAI435/CGNAT-DPDK-ISP.git
cd CGNAT-DPDK-ISP
```

**Step 6: Build CGNAT**
```bash
meson setup build
ninja -C build
```

**Step 7: Configure Network Interfaces**
```bash
# List available network interfaces
cd ~/dpdk-23.11/build
sudo ./app/dpdk-devbind.py --status

# Example output:
# 0000:00:05.0 'Elastic Network Adapter (ENA)' if=eth0 drv=ena unused=vfio-pci *Active*
# 0000:00:06.0 'Elastic Network Adapter (ENA)' if=eth1 drv=ena unused=vfio-pci
# 0000:00:07.0 'Elastic Network Adapter (ENA)' if=eth2 drv=ena unused=vfio-pci

# Bind eth1 and eth2 to DPDK (KEEP eth0 for SSH!)
sudo ./app/dpdk-devbind.py --bind=vfio-pci 0000:00:06.0 0000:00:07.0

# Verify binding
sudo ./app/dpdk-devbind.py --status
```

**Step 8: Configure CGNAT**
```bash
cd ~/CGNAT-DPDK-ISP

# Edit configuration file
nano config/cgnat.yaml

# Update these settings:
dpdk:
  core_mask: "0xFF"          # Use 8 cores (cores 0-7)
  memory_channels: 4
  port_mask: 0x3             # Use ports 0 and 1 (eth1, eth2)
  
nat:
  public_ips:
    - "10.0.2.100"           # Use private IP from AWS subnet
    - "10.0.2.101"
  customer_network: "10.0.1.0/24"
  ports_per_customer: 1024
  
telemetry:
  prometheus_port: 9091
  api_port: 8080
```

**Step 9: Run CGNAT**
```bash
cd ~/CGNAT-DPDK-ISP/build

# Run with 8 cores, 4 memory channels, using ports eth1+eth2
sudo ./dpdk-cgnat -c 0xFF -n 4 -- -p 0x3 -q 8

# Expected output:
# [INFO] DPDK CGNAT v0.1 starting...
# [INFO] 8 worker cores initialized
# [INFO] NAT table size: 50000 entries per core
# [INFO] Port pool: 645,120 ports per public IP
# [INFO] Prometheus metrics: http://0.0.0.0:9091/metrics
# [INFO] Web dashboard: http://0.0.0.0:8080
# [INFO] DPDK CGNAT ready!
```

---

### Phase 4: Access Monitoring Dashboard

#### From Your Local Computer

```bash
# Create SSH tunnel to access dashboard
ssh -i your-key.pem -L 8080:localhost:8080 -L 9091:localhost:9091 \
    ubuntu@ec2-XX-XXX-XXX-XX.compute.amazonaws.com

# Now open in browser:
# Dashboard: http://localhost:8080
# Prometheus: http://localhost:9091/metrics
```

#### Or Configure Security Group (Public Access)

1. Go to **EC2 → Security Groups**
2. Edit **dpdk-cgnat-sg** inbound rules
3. Add rules:
   - Type: Custom TCP, Port: 8080, Source: 0.0.0.0/0 (Dashboard)
   - Type: Custom TCP, Port: 9091, Source: 0.0.0.0/0 (Prometheus)

4. Access directly:
   - **Dashboard**: `http://your-ec2-public-ip:8080`
   - **Prometheus**: `http://your-ec2-public-ip:9091/metrics`

---

### Phase 5: Performance Testing

#### Test with DPDK Pktgen

```bash
# Install pktgen-dpdk for traffic generation
cd ~
git clone http://dpdk.org/git/apps/pktgen-dpdk
cd pktgen-dpdk
make

# Generate test traffic (10 Gbps UDP)
sudo ./app/x86_64-native-linux-gcc/pktgen -c 0x1F -n 4 -- \
    -P -m "[1:2].0" -f test/basic.pkt

# Configure packet stream
Pktgen> set 0 rate 100
Pktgen> start 0
```

#### Monitor Performance

```bash
# Watch live metrics
watch -n 1 'curl -s http://localhost:9091/metrics | grep cgnat'

# Expected metrics:
# cgnat_packets_processed_total 5600000   (5.6M pps)
# cgnat_sessions_active 20000
# cgnat_avg_latency_us 8.5
```

---

## 💰 Cost Optimization

### Use Spot Instances (60-80% Savings)

```bash
# Launch Spot instance via AWS CLI
aws ec2 request-spot-instances \
    --instance-count 1 \
    --type "one-time" \
    --launch-specification file://spot-config.json

# spot-config.json:
{
  "ImageId": "ami-xxxxxxxxx",
  "InstanceType": "c5.4xlarge",
  "KeyName": "your-key",
  "SecurityGroupIds": ["sg-xxxxx"]
}
```

### Estimated Monthly Costs

| Instance Type | On-Demand | Spot Price | Monthly (Spot) |
|---------------|-----------|------------|----------------|
| c5.2xlarge | $0.34/hr | $0.10/hr | ~$72 |
| c5.4xlarge | $0.68/hr | $0.20/hr | ~$144 |
| c5n.9xlarge | $1.94/hr | $0.60/hr | ~$432 |

**Plus:**
- Storage: ~$5/month (50GB gp3)
- Data Transfer: ~$10-50/month (1-5 TB out)

**Total Testing Cost**: **$150-500/month** with Spot instances

---

## 🔧 Troubleshooting

### Issue: "Cannot allocate memory" when starting DPDK

**Solution**: Increase huge pages
```bash
echo 8192 | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
```

### Issue: "No Ethernet ports found"

**Solution**: Verify interfaces are bound to DPDK
```bash
sudo ~/dpdk-23.11/build/app/dpdk-devbind.py --status
```

### Issue: Lost SSH connection after binding eth0

**⚠️ CRITICAL**: Never bind eth0! Always keep it for SSH access.
```bash
# Only bind eth1, eth2, eth3, etc.
sudo ./dpdk-devbind.py --bind=vfio-pci 0000:00:06.0 0000:00:07.0
```

### Issue: Dashboard not accessible

**Solution**: Check security group rules allow port 8080
```bash
# Or use SSH tunnel
ssh -L 8080:localhost:8080 ubuntu@your-ec2-ip
```

---

## 📊 Alternative: Azure Deployment

For Azure, use **F-series** or **Fsv2-series** with Mellanox NICs:

```bash
# Create VM
az vm create \
  --resource-group dpdk-rg \
  --name dpdk-cgnat \
  --image UbuntuLTS \
  --size Standard_F16s_v2 \
  --admin-username azureuser \
  --generate-ssh-keys \
  --accelerated-networking true

# Enable DPDK
sudo apt-get install -y dpdk dpdk-dev
# Follow similar setup steps as AWS
```

**Costs**: ~$0.67/hour (Standard_F16s_v2) = ~$483/month

---

## 🎯 Next Steps After Setup

1. ✅ **Verify DPDK is running** - Check logs for "CGNAT ready"
2. ✅ **Access dashboard** - Open browser to port 8080
3. ✅ **Generate test traffic** - Use pktgen or real customer traffic
4. ✅ **Monitor metrics** - Check Prometheus on port 9091
5. ✅ **Performance tuning** - Adjust core count, memory channels
6. ✅ **Save AMI** - Create custom AMI for quick redeployment

---

## 📚 Additional Resources

- **AWS EC2 DPDK Tutorial**: https://github.com/NEOAdvancedTechnology/MinimalDPDKExamples
- **DPDK ENA Driver Docs**: http://doc.dpdk.org/guides/nics/ena.html
- **AWS Enhanced Networking**: https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/enhanced-networking-ena.html
- **DPDK Performance Guide**: https://doc.dpdk.org/guides/linux_gsg/nic_perf_intel_platform.html

---

## 🛟 Support

If you encounter issues:
1. Check logs: `sudo dmesg | tail -50`
2. Verify huge pages: `grep Huge /proc/meminfo`
3. Check interface status: `sudo dpdk-devbind.py --status`
4. Review security groups and firewall rules

**For production deployment, see**: `docs/DEPLOYMENT.md`

---

**Ready to deploy?** Start with AWS c5.4xlarge Spot instance and follow the automated setup! 🚀
