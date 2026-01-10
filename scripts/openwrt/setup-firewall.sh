#!/bin/sh
# StreamGuard - OpenWrt firewall setup
#
# Auto-detects whether to use nftables (fw4) or iptables/ipset (fw3).
# Run this script on your OpenWrt router to create the firewall rules
# needed for StreamGuard quota enforcement.
#
# Usage: sh setup-firewall.sh [nft|ipset]
#   nft   - Force nftables (OpenWrt 22.03+)
#   ipset - Force iptables/ipset (OpenWrt 21.02 and older)

set -e

# Auto-detect firewall type
detect_firewall() {
    if command -v nft >/dev/null 2>&1 && nft list table inet fw4 >/dev/null 2>&1; then
        echo "nft"
    elif command -v ipset >/dev/null 2>&1; then
        echo "ipset"
    else
        echo "unknown"
    fi
}

# Setup for nftables (fw4)
setup_nftables() {
    echo "Setting up StreamGuard with nftables (fw4)..."
    echo ""

    # Create the sets if they don't exist
    nft add set inet fw4 blocked_streaming_clients '{ type ipv4_addr; flags timeout; timeout 24h; }' 2>/dev/null || true
    nft add set inet fw4 streaming_destinations '{ type ipv4_addr; flags timeout; timeout 24h; }' 2>/dev/null || true

    # Create the chain if it doesn't exist
    nft add chain inet fw4 stream_quota '{ type filter hook forward priority 0; policy accept; }' 2>/dev/null || true

    # Add the drop rule (check if it exists first)
    if ! nft list chain inet fw4 stream_quota 2>/dev/null | grep -q "blocked_streaming_clients"; then
        nft add rule inet fw4 stream_quota ip saddr @blocked_streaming_clients ip daddr @streaming_destinations drop
        echo "Added drop rule for blocked clients"
    else
        echo "Drop rule already exists"
    fi

    echo ""
    echo "nftables setup complete!"
    echo ""
    echo "Use StreamGuard with: -F nft (or omit -F, nft is default)"
    echo ""
    echo "To view blocked clients:"
    echo "  nft list set inet fw4 blocked_streaming_clients"
    echo ""
    echo "To manually block a client:"
    echo "  nft add element inet fw4 blocked_streaming_clients '{ 192.168.0.10 timeout 1h }'"
    echo ""
    echo "To manually unblock a client:"
    echo "  nft delete element inet fw4 blocked_streaming_clients '{ 192.168.0.10 }'"
}

# Setup for iptables/ipset (fw3)
setup_ipset() {
    echo "Setting up StreamGuard with iptables/ipset (fw3)..."
    echo ""

    # Check if ipset is installed
    if ! command -v ipset >/dev/null 2>&1; then
        echo "ERROR: ipset not found. Install it with:"
        echo "  opkg update && opkg install ipset iptables-mod-ipset kmod-ipt-ipset"
        exit 1
    fi

    # Create IP sets (timeout in seconds: 86400=24h)
    # The -! flag means don't error if set already exists
    ipset create blocked_streaming_clients hash:ip timeout 86400 2>/dev/null || true
    ipset create streaming_destinations hash:ip timeout 86400 2>/dev/null || true

    echo "Created ipsets:"
    echo "  - blocked_streaming_clients (24h timeout)"
    echo "  - streaming_destinations (24h timeout)"

    # Add iptables rule if it doesn't exist
    if ! iptables -C FORWARD -m set --match-set blocked_streaming_clients src -m set --match-set streaming_destinations dst -j DROP 2>/dev/null; then
        iptables -I FORWARD -m set --match-set blocked_streaming_clients src -m set --match-set streaming_destinations dst -j DROP
        echo "Added iptables DROP rule"
    else
        echo "iptables DROP rule already exists"
    fi

    # Make persistent across reboots - add to /etc/firewall.user
    FIREWALL_USER="/etc/firewall.user"
    MARKER="# StreamGuard ipset rules"

    if ! grep -q "$MARKER" "$FIREWALL_USER" 2>/dev/null; then
        echo ""
        echo "Adding persistent rules to $FIREWALL_USER..."
        cat >> "$FIREWALL_USER" << 'EOF'

# StreamGuard ipset rules
ipset create blocked_streaming_clients hash:ip timeout 86400 2>/dev/null || true
ipset create streaming_destinations hash:ip timeout 86400 2>/dev/null || true
iptables -C FORWARD -m set --match-set blocked_streaming_clients src -m set --match-set streaming_destinations dst -j DROP 2>/dev/null || \
iptables -I FORWARD -m set --match-set blocked_streaming_clients src -m set --match-set streaming_destinations dst -j DROP
EOF
        echo "Added persistent rules to $FIREWALL_USER"
    else
        echo "Persistent rules already in $FIREWALL_USER"
    fi

    echo ""
    echo "ipset setup complete!"
    echo ""
    echo "Use StreamGuard with: -F ipset"
    echo ""
    echo "To view blocked clients:"
    echo "  ipset list blocked_streaming_clients"
    echo ""
    echo "To manually block a client:"
    echo "  ipset add blocked_streaming_clients 192.168.0.10"
    echo ""
    echo "To manually unblock a client:"
    echo "  ipset del blocked_streaming_clients 192.168.0.10"
}

# Main
FW_TYPE="${1:-auto}"

if [ "$FW_TYPE" = "auto" ]; then
    FW_TYPE=$(detect_firewall)
    echo "Auto-detected firewall type: $FW_TYPE"
    echo ""
fi

case "$FW_TYPE" in
    nft|nftables)
        setup_nftables
        ;;
    ipset|iptables)
        setup_ipset
        ;;
    *)
        echo "ERROR: Could not detect firewall type."
        echo ""
        echo "For OpenWrt 22.03+ (fw4/nftables):"
        echo "  sh setup-firewall.sh nft"
        echo ""
        echo "For OpenWrt 21.02 and older (fw3/iptables):"
        echo "  First install ipset: opkg update && opkg install ipset iptables-mod-ipset kmod-ipt-ipset"
        echo "  Then run: sh setup-firewall.sh ipset"
        exit 1
        ;;
esac
