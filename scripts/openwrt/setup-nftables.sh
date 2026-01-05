#!/bin/sh
# StreamGuard - OpenWrt nftables setup
#
# Run this script on your OpenWrt router to create the firewall rules
# needed for StreamGuard quota enforcement.
#
# Usage: sh setup-nftables.sh

set -e

echo "Setting up StreamGuard nftables rules..."

# Create the sets if they don't exist
nft add set inet fw4 blocked_streaming_clients '{ type ipv4_addr; flags timeout; timeout 24h; }' 2>/dev/null || true

nft add set inet fw4 streaming_destinations '{ type ipv4_addr; flags timeout; timeout 1h; }' 2>/dev/null || true

# Create the chain if it doesn't exist
nft add chain inet fw4 stream_quota '{ type filter hook forward priority 0; policy accept; }' 2>/dev/null || true

# Add the drop rule (check if it exists first)
if ! nft list chain inet fw4 stream_quota | grep -q "blocked_streaming_clients"; then
    nft add rule inet fw4 stream_quota ip saddr @blocked_streaming_clients ip daddr @streaming_destinations drop
    echo "Added drop rule for blocked clients"
else
    echo "Drop rule already exists"
fi

echo ""
echo "StreamGuard nftables setup complete!"
echo ""
echo "To view blocked clients:"
echo "  nft list set inet fw4 blocked_streaming_clients"
echo ""
echo "To view streaming destinations (populated by dnsmasq):"
echo "  nft list set inet fw4 streaming_destinations"
echo ""
echo "To manually block a client:"
echo "  nft add element inet fw4 blocked_streaming_clients '{ 192.168.0.10 timeout 1h }'"
echo ""
echo "To manually unblock a client:"
echo "  nft delete element inet fw4 blocked_streaming_clients '{ 192.168.0.10 }'"
