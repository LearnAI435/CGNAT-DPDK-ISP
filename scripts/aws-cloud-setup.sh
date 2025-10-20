#!/bin/bash

################################################################################
# DPDK CGNAT AWS EC2 Automated Setup Script
# 
# This script automates the complete installation and configuration of
# DPDK CGNAT on AWS EC2 instances with ENA networking.
#
# Usage: curl -fsSL https://raw.githubusercontent.com/LearnAI435/CGNAT-DPDK-ISP/main/scripts/aws-cloud-setup.sh | sudo bash
#
# Or download and run:
#   wget https://raw.githubusercontent.com/LearnAI435/CGNAT-DPDK-ISP/main/scripts/aws-cloud-setup.sh
#   chmod +x aws-cloud-setup.sh
#   sudo ./aws-cloud-setup.sh
################################################################################

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

DPDK_VERSION="23.11"
CGNAT_REPO="https://github.com/LearnAI435/CGNAT-DPDK-ISP.git"
INSTALL_DIR="/opt/dpdk-cgnat"

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_banner() {
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "  DPDK CGNAT Automated Setup for AWS EC2"
    echo "  Version: 0.1"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
}

check_root() {
    if [[ $EUID -ne 0 ]]; then
        log_error "This script must be run as root (use sudo)"
        exit 1
    fi
}

detect_os() {
    log_info "Detecting operating system..."
    
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        OS=$ID
        OS_VERSION=$VERSION_ID
    else
        log_error "Cannot detect OS. Please use Ubuntu 22.04+ or Amazon Linux 2023"
        exit 1
    fi
    
    log_success "Detected: $PRETTY_NAME"
}

install_dependencies() {
    log_info "Installing system dependencies..."
    
    if [[ "$OS" == "ubuntu" ]] || [[ "$OS" == "debian" ]]; then
        apt-get update -qq
        apt-get install -y -qq \
            build-essential git wget curl \
            libnuma-dev python3-pyelftools python3-pip \
            pkg-config meson ninja-build \
            libyaml-dev libmicrohttpd-dev \
            pciutils ethtool net-tools \
            linux-headers-$(uname -r) > /dev/null 2>&1
    elif [[ "$OS" == "amzn" ]] || [[ "$OS" == "rhel" ]] || [[ "$OS" == "centos" ]]; then
        yum groupinstall -y -q "Development Tools"
        yum install -y -q \
            numactl-devel python3-pyelftools python3-pip \
            pkgconfig meson ninja-build \
            libyaml-devel libmicrohttpd-devel \
            pciutils ethtool net-tools \
            kernel-devel-$(uname -r) > /dev/null 2>&1
    else
        log_error "Unsupported OS: $OS"
        exit 1
    fi
    
    log_success "Dependencies installed"
}

download_and_build_dpdk() {
    log_info "Downloading DPDK $DPDK_VERSION..."
    
    cd /opt
    if [ -d "dpdk-${DPDK_VERSION}" ]; then
        log_warning "DPDK already exists, skipping download"
    else
        wget -q https://fast.dpdk.org/rel/dpdk-${DPDK_VERSION}.tar.xz
        tar xf dpdk-${DPDK_VERSION}.tar.xz
        rm dpdk-${DPDK_VERSION}.tar.xz
    fi
    
    cd dpdk-${DPDK_VERSION}
    
    log_info "Building DPDK (this may take 5-10 minutes)..."
    
    if [ -d "build" ]; then
        log_warning "DPDK build directory exists, cleaning..."
        rm -rf build
    fi
    
    meson setup build > /dev/null 2>&1
    cd build
    ninja > /dev/null 2>&1
    ninja install > /dev/null 2>&1
    ldconfig
    
    log_success "DPDK $DPDK_VERSION built and installed"
}

setup_hugepages() {
    log_info "Configuring huge pages (8GB with 2MB pages)..."
    
    local HUGEPAGES_COUNT=4096
    
    echo $HUGEPAGES_COUNT > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    
    mkdir -p /mnt/huge
    if ! grep -q "/mnt/huge" /etc/fstab; then
        echo "nodev /mnt/huge hugetlbfs defaults 0 0" >> /etc/fstab
    fi
    mount -t hugetlbfs nodev /mnt/huge 2>/dev/null || true
    
    ALLOCATED=$(cat /proc/meminfo | grep HugePages_Total | awk '{print $2}')
    log_success "Huge pages configured: $ALLOCATED pages ($(($ALLOCATED * 2))MB)"
}

load_kernel_modules() {
    log_info "Loading DPDK kernel modules..."
    
    modprobe vfio-pci || log_warning "vfio-pci module not available"
    modprobe uio || log_warning "uio module not available"
    
    if [ -e /sys/module/vfio/parameters/enable_unsafe_noiommu_mode ]; then
        echo 1 > /sys/module/vfio/parameters/enable_unsafe_noiommu_mode
        log_success "Enabled vfio-pci no-IOMMU mode (required for non-metal EC2)"
    fi
    
    log_success "Kernel modules loaded"
}

clone_cgnat_repo() {
    log_info "Cloning DPDK CGNAT repository..."
    
    if [ -d "$INSTALL_DIR" ]; then
        log_warning "CGNAT directory exists, pulling latest changes..."
        cd $INSTALL_DIR
        git pull -q
    else
        git clone -q $CGNAT_REPO $INSTALL_DIR
    fi
    
    log_success "Repository cloned to $INSTALL_DIR"
}

build_cgnat() {
    log_info "Building DPDK CGNAT..."
    
    cd $INSTALL_DIR
    
    if [ -d "build" ]; then
        rm -rf build
    fi
    
    meson setup build > /dev/null 2>&1
    ninja -C build > /dev/null 2>&1
    
    log_success "DPDK CGNAT built successfully"
}

detect_network_interfaces() {
    log_info "Detecting network interfaces..."
    
    /opt/dpdk-${DPDK_VERSION}/build/app/dpdk-devbind.py --status
    
    echo ""
    log_warning "⚠️  IMPORTANT: Do NOT bind eth0 (your SSH interface)!"
    echo ""
}

create_config() {
    log_info "Creating default configuration..."
    
    cat > $INSTALL_DIR/config/cgnat-aws.yaml <<EOF
# DPDK CGNAT Configuration for AWS EC2
# Auto-generated by aws-cloud-setup.sh

dpdk:
  core_mask: "0xFF"              # Use 8 cores (adjust based on instance type)
  memory_channels: 4
  port_mask: 0x3                 # Use 2 ports (eth1, eth2)
  huge_page_dir: "/mnt/huge"
  
nat:
  public_ips:
    - "10.0.2.100"               # TODO: Update with your AWS private IPs
    - "10.0.2.101"
    - "10.0.2.102"
    - "10.0.2.103"
    - "10.0.2.104"
    - "10.0.2.105"
    - "10.0.2.106"
    - "10.0.2.107"
    - "10.0.2.108"
    - "10.0.2.109"
    
  customer_network: "10.0.1.0/24"  # TODO: Update with your customer subnet
  
  ports_per_customer: 1024
  tcp_timeout: 7200              # 2 hours
  udp_timeout: 300               # 5 minutes
  icmp_timeout: 60               # 1 minute
  
  max_sessions_per_core: 50000
  hash_table_size: 65536
  
telemetry:
  prometheus_port: 9091
  api_port: 8080
  update_interval: 5             # seconds
  
logging:
  level: "INFO"                  # DEBUG, INFO, WARNING, ERROR
  file: "/var/log/dpdk-cgnat.log"
EOF
    
    log_success "Configuration created at $INSTALL_DIR/config/cgnat-aws.yaml"
}

create_systemd_service() {
    log_info "Creating systemd service..."
    
    cat > /etc/systemd/system/dpdk-cgnat.service <<EOF
[Unit]
Description=DPDK CGNAT Service
After=network.target

[Service]
Type=simple
User=root
WorkingDirectory=$INSTALL_DIR
ExecStart=$INSTALL_DIR/build/dpdk-cgnat -c 0xFF -n 4 -- -p 0x3 -q 8
Restart=on-failure
RestartSec=10
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF
    
    systemctl daemon-reload
    
    log_success "Systemd service created (use 'systemctl start dpdk-cgnat')"
}

create_helper_scripts() {
    log_info "Creating helper scripts..."
    
    cat > $INSTALL_DIR/bind-interfaces.sh <<'EOF'
#!/bin/bash
# Bind network interfaces to DPDK

DPDK_DIR="/opt/dpdk-23.11"

echo "Available network interfaces:"
$DPDK_DIR/build/app/dpdk-devbind.py --status

echo ""
echo "WARNING: Do NOT bind eth0 (SSH interface)!"
echo ""
read -p "Enter PCI addresses to bind (space-separated, e.g., 0000:00:06.0 0000:00:07.0): " PCI_ADDRS

if [ -z "$PCI_ADDRS" ]; then
    echo "No interfaces specified. Exiting."
    exit 1
fi

echo "Binding interfaces to vfio-pci..."
$DPDK_DIR/build/app/dpdk-devbind.py --bind=vfio-pci $PCI_ADDRS

echo ""
echo "Interfaces bound successfully!"
$DPDK_DIR/build/app/dpdk-devbind.py --status
EOF
    
    chmod +x $INSTALL_DIR/bind-interfaces.sh
    
    cat > $INSTALL_DIR/unbind-interfaces.sh <<'EOF'
#!/bin/bash
# Unbind DPDK interfaces back to kernel

DPDK_DIR="/opt/dpdk-23.11"

echo "DPDK-bound interfaces:"
$DPDK_DIR/build/app/dpdk-devbind.py --status | grep vfio-pci

echo ""
read -p "Enter PCI addresses to unbind (space-separated): " PCI_ADDRS

if [ -z "$PCI_ADDRS" ]; then
    echo "No interfaces specified. Exiting."
    exit 1
fi

echo "Unbinding interfaces back to ENA driver..."
$DPDK_DIR/build/app/dpdk-devbind.py --bind=ena $PCI_ADDRS

echo ""
echo "Interfaces unbound successfully!"
$DPDK_DIR/build/app/dpdk-devbind.py --status
EOF
    
    chmod +x $INSTALL_DIR/unbind-interfaces.sh
    
    log_success "Helper scripts created in $INSTALL_DIR"
}

print_next_steps() {
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    log_success "🎉 DPDK CGNAT Setup Complete!"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
    echo "📁 Installation Directory: $INSTALL_DIR"
    echo "📝 Configuration File: $INSTALL_DIR/config/cgnat-aws.yaml"
    echo ""
    echo "🚀 Next Steps:"
    echo ""
    echo "1. Bind network interfaces to DPDK:"
    echo "   cd $INSTALL_DIR"
    echo "   sudo ./bind-interfaces.sh"
    echo ""
    echo "2. Edit configuration file:"
    echo "   sudo nano $INSTALL_DIR/config/cgnat-aws.yaml"
    echo "   (Update public_ips and customer_network)"
    echo ""
    echo "3. Run DPDK CGNAT manually:"
    echo "   cd $INSTALL_DIR/build"
    echo "   sudo ./dpdk-cgnat -c 0xFF -n 4 -- -p 0x3 -q 8"
    echo ""
    echo "4. Or enable as systemd service:"
    echo "   sudo systemctl enable dpdk-cgnat"
    echo "   sudo systemctl start dpdk-cgnat"
    echo "   sudo systemctl status dpdk-cgnat"
    echo ""
    echo "📊 Access Monitoring:"
    echo "   Dashboard: http://$(curl -s http://169.254.169.254/latest/meta-data/public-ipv4):8080"
    echo "   Prometheus: http://$(curl -s http://169.254.169.254/latest/meta-data/public-ipv4):9091/metrics"
    echo ""
    echo "🛠️  Useful Commands:"
    echo "   View huge pages: grep Huge /proc/meminfo"
    echo "   Check interface status: /opt/dpdk-${DPDK_VERSION}/build/app/dpdk-devbind.py --status"
    echo "   View logs: sudo journalctl -u dpdk-cgnat -f"
    echo "   Unbind interfaces: sudo $INSTALL_DIR/unbind-interfaces.sh"
    echo ""
    echo "📚 Documentation: $INSTALL_DIR/docs/"
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
}

main() {
    print_banner
    check_root
    detect_os
    install_dependencies
    download_and_build_dpdk
    setup_hugepages
    load_kernel_modules
    clone_cgnat_repo
    build_cgnat
    create_config
    create_systemd_service
    create_helper_scripts
    detect_network_interfaces
    print_next_steps
}

main "$@"
