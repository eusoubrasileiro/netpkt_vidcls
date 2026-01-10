#!/bin/sh
# StreamGuard - Install configuration on OpenWrt router
#
# Run this script from Ubuntu to deploy the firewall, dnsmasq, and
# rpcapd configuration to your OpenWrt router.
#
# Usage: sh install.sh [router_ip]
#
# Prerequisites:
# - SSH key-based authentication to router (ssh-copy-id root@router)
# - dnsmasq installed on router (default on OpenWrt)

ROUTER_IP="${1:-192.168.0.1}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "StreamGuard OpenWrt Installation"
echo "================================"
echo "Router: $ROUTER_IP"
echo ""

# Test SSH connection
echo "Testing SSH connection..."
if ! ssh -o ConnectTimeout=5 root@$ROUTER_IP "echo OK" >/dev/null 2>&1; then
    echo "ERROR: Cannot connect to router via SSH."
    echo "Make sure you have SSH key auth set up:"
    echo "  ssh-copy-id root@$ROUTER_IP"
    exit 1
fi
echo "SSH connection OK"
echo ""

# Helper function: copy file via cat (more reliable than scp on OpenWrt)
copy_file() {
    local src="$1"
    local dst="$2"
    cat "$src" | ssh root@$ROUTER_IP "cat > $dst"
}

# Ensure dnsmasq.d directory exists
echo "Creating dnsmasq.d directory..."
ssh root@$ROUTER_IP "mkdir -p /etc/dnsmasq.d"
echo "Done"

# Copy dnsmasq config
echo "Installing dnsmasq configuration..."
copy_file "$SCRIPT_DIR/dnsmasq-streaming.conf" "/etc/dnsmasq.d/dnsmasq-streaming.conf"
echo "Done"

# Copy and run firewall setup (auto-detects nftables vs ipset)
echo "Installing firewall rules..."
copy_file "$SCRIPT_DIR/setup-firewall.sh" "/tmp/setup-firewall.sh"
ssh root@$ROUTER_IP "sh /tmp/setup-firewall.sh"
echo "Done"

# Install rpcapd for remote packet capture
echo "Installing rpcapd (remote packet capture daemon)..."
ssh root@$ROUTER_IP "opkg update && opkg install rpcapd" 2>/dev/null || {
    echo "WARNING: Could not install rpcapd via opkg."
    echo "You may need to install it manually: opkg install rpcapd"
}
echo "Done"

# Create init script (OpenWrt package doesn't include one)
echo "Creating rpcapd init script..."
ssh root@$ROUTER_IP 'cat > /etc/init.d/rpcapd << "INITEOF"
#!/bin/sh /etc/rc.common

START=95
STOP=10
USE_PROCD=1

start_service() {
    procd_open_instance
    procd_set_param command /usr/bin/rpcapd -n -d
    procd_set_param respawn
    procd_close_instance
}
INITEOF
chmod +x /etc/init.d/rpcapd'
echo "Done"

# Enable and start rpcapd
echo "Enabling rpcapd service..."
ssh root@$ROUTER_IP "/etc/init.d/rpcapd enable 2>/dev/null || true"
echo "Done"

# Restart dnsmasq
echo "Restarting dnsmasq..."
ssh root@$ROUTER_IP "/etc/init.d/dnsmasq restart"
echo "Done"

# Start rpcapd
echo "Starting rpcapd service..."
ssh root@$ROUTER_IP "/etc/init.d/rpcapd start 2>/dev/null || /etc/init.d/rpcapd restart 2>/dev/null || true"
echo "Done"

echo ""
echo "Installation complete!"
echo ""
echo "The router is now configured to:"
echo "1. Track IPs of streaming domains (via dnsmasq)"
echo "2. Block clients when StreamGuard adds them to blocked set"
echo "3. Serve remote packet capture via rpcapd (port 2002)"
echo ""
echo "Start StreamGuard on this machine:"
echo "  sudo ./streamguard -r rpcap://$ROUTER_IP/br-lan -e -R $ROUTER_IP"
echo ""
echo "Or via systemd:"
echo "  sudo systemctl start streamguard"
echo ""
echo "To check rpcapd status on router:"
echo "  ssh root@$ROUTER_IP '/etc/init.d/rpcapd status'"
echo ""
echo "To monitor blocked clients on router:"
echo "  ssh root@$ROUTER_IP 'nft list set inet fw4 blocked_streaming_clients'"
echo "  # or for older OpenWrt:"
echo "  ssh root@$ROUTER_IP 'ipset list blocked_streaming_clients'"
