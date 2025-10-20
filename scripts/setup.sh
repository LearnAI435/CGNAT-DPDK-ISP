#!/bin/bash
# DPDK CGNAT Quick Setup Script

set -e

echo "================================"
echo "DPDK CGNAT Setup Script"
echo "================================"

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (sudo)"
    exit 1
fi

# Detect OS
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$ID
else
    echo "Cannot detect OS"
    exit 1
fi

echo "Detected OS: $OS"

# Install dependencies
echo "Installing dependencies..."
if [ "$OS" = "ubuntu" ] || [ "$OS" = "debian" ]; then
    apt update
    apt install -y build-essential meson ninja-build python3-pyelftools
    apt install -y libnuma-dev libpcap-dev pkg-config libyaml-dev
    apt install -y linux-headers-$(uname -r)
elif [ "$OS" = "rhel" ] || [ "$OS" = "centos" ]; then
    yum groupinstall -y "Development Tools"
    yum install -y meson ninja-build python3-pyelftools
    yum install -y numactl-devel libpcap-devel yaml-devel
    yum install -y kernel-devel
else
    echo "Unsupported OS: $OS"
    exit 1
fi

# Configure huge pages
echo "Configuring huge pages..."
echo 8192 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
mkdir -p /mnt/huge
mount -t hugetlbfs nodev /mnt/huge

# Add to fstab if not already there
if ! grep -q "/mnt/huge" /etc/fstab; then
    echo "nodev /mnt/huge hugetlbfs defaults 0 0" >> /etc/fstab
fi

# Load VFIO driver
echo "Loading VFIO driver..."
modprobe vfio-pci
if ! grep -q "vfio-pci" /etc/modules; then
    echo "vfio-pci" >> /etc/modules
fi

echo ""
echo "================================"
echo "Setup complete!"
echo "================================"
echo ""
echo "Next steps:"
echo "1. Reboot to apply GRUB changes:"
echo "   - Edit /etc/default/grub"
echo "   - Add: intel_iommu=on iommu=pt (or amd_iommu=on)"
echo "   - Run: update-grub && reboot"
echo ""
echo "2. Bind NIC to DPDK:"
echo "   - Find NIC: lspci | grep Ethernet"
echo "   - Bind: dpdk-devbind.py --bind=vfio-pci <PCI_ADDRESS>"
echo ""
echo "3. Build DPDK CGNAT:"
echo "   - cd /opt/dpdk-cgnat"
echo "   - meson setup build && ninja -C build"
echo ""
echo "4. Run:"
echo "   - sudo ./build/dpdk-cgnat -c 0xff -n 4 -- -p 0x1 -q 8"
echo ""
