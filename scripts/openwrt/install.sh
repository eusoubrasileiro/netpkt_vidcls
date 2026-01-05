#!/bin/sh
# StreamGuard - Install configuration on OpenWrt router
#
# Run this script from the Orange Pi (or wherever StreamGuard runs) to
# deploy the firewall and dnsmasq configuration to your OpenWrt router.
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

# Copy dnsmasq config
echo "Installing dnsmasq configuration..."
scp "$SCRIPT_DIR/dnsmasq-streaming.conf" root@$ROUTER_IP:/etc/dnsmasq.d/
echo "Done"

# Copy and run nftables setup
echo "Installing nftables rules..."
scp "$SCRIPT_DIR/setup-nftables.sh" root@$ROUTER_IP:/tmp/
ssh root@$ROUTER_IP "sh /tmp/setup-nftables.sh"
echo "Done"

# Make nftables rules persistent
echo "Making rules persistent..."
ssh root@$ROUTER_IP "cat > /etc/nftables.d/30-streamguard.nft << 'EOF'
# StreamGuard quota enforcement sets
add set inet fw4 blocked_streaming_clients { type ipv4_addr; flags timeout; timeout 24h; }
add set inet fw4 streaming_destinations { type ipv4_addr; flags timeout; timeout 1h; }
add chain inet fw4 stream_quota { type filter hook forward priority 0; policy accept; }
add rule inet fw4 stream_quota ip saddr @blocked_streaming_clients ip daddr @streaming_destinations drop
EOF"
echo "Done"

# Restart services
echo "Restarting services..."
ssh root@$ROUTER_IP "service dnsmasq restart && fw4 reload"
echo "Done"

echo ""
echo "Installation complete!"
echo ""
echo "The router is now configured to:"
echo "1. Track IPs of streaming domains (via dnsmasq)"
echo "2. Block clients added to the blocked_streaming_clients set"
echo ""
echo "Start StreamGuard on this machine:"
echo "  sudo ./streamguard -i eth0 -e"
echo ""
echo "To monitor blocked clients on router:"
echo "  ssh root@$ROUTER_IP 'nft list set inet fw4 blocked_streaming_clients'"
