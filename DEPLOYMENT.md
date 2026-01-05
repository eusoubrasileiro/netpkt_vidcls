# StreamGuard Deployment Guide

Complete step-by-step guide to deploy StreamGuard on Ubuntu with an OpenWrt router.

## Your Setup

This guide assumes:
- **Ubuntu machine**: Desktop/server on your LAN (interface: `eno1`)
- **OpenWrt router**: Gateway at `192.168.0.1`
- **LAN subnet**: `192.168.0.0/24`

Adjust values as needed for your network.

---

## Prerequisites

### Ubuntu Machine

Install development dependencies:

```bash
sudo apt update
sudo apt install libndpi-dev libpcap-dev libcjson-dev build-essential
```

### OpenWrt Router

1. **Enable SSH root access** (should be enabled by default)

2. **Install tcpdump** (optional, for debugging):
   ```bash
   ssh root@192.168.0.1
   opkg update
   opkg install tcpdump
   ```

---

## Step 1: Setup SSH Key Authentication

StreamGuard needs passwordless SSH access to add/remove blocked IPs on the router.

```bash
# Generate SSH key if you don't have one
ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519

# Copy to router
ssh-copy-id root@192.168.0.1

# Test - should work without password prompt
ssh root@192.168.0.1 "echo 'SSH OK'"
```

If `ssh-copy-id` doesn't work, manually add your public key:

```bash
cat ~/.ssh/id_ed25519.pub | ssh root@192.168.0.1 "cat >> /etc/dropbear/authorized_keys"
```

---

## Step 2: Build StreamGuard

```bash
cd /path/to/streamguard/src
make clean
make

# Verify it built
./streamguard -h
```

Expected output:
```
Usage: ./streamguard -i <interface> [options]
...
```

### Optional: Install system-wide

```bash
sudo make install  # Installs to /usr/local/bin/
```

---

## Step 3: Configure OpenWrt Router

Run the install script to set up firewall rules and dnsmasq configuration:

```bash
cd /path/to/streamguard/scripts/openwrt
./install.sh 192.168.0.1
```

This script:
1. Creates nftables sets for blocked clients and streaming destinations
2. Adds firewall rule to drop blocked traffic
3. Configures dnsmasq to track streaming domain IPs
4. Makes configuration persistent across reboots

### What Gets Installed on Router

**Firewall rules** (`/etc/nftables.d/30-streamguard.nft`):
- `blocked_streaming_clients` - Set of client IPs that exceeded quota
- `streaming_destinations` - Set of streaming service IPs (auto-populated by dnsmasq)
- DROP rule matching blocked clients to streaming destinations

**dnsmasq config** (`/etc/dnsmasq.d/dnsmasq-streaming.conf`):
- Adds resolved IPs for YouTube, Netflix, TikTok, etc. to the nftables set

### Verify Router Configuration

```bash
# Check sets exist
ssh root@192.168.0.1 'nft list sets inet fw4'

# Should show:
# set blocked_streaming_clients { type ipv4_addr; ... }
# set streaming_destinations { type ipv4_addr; ... }
```

---

## Step 4: Test in Dry-Run Mode

Start StreamGuard without enforcement to verify detection works:

```bash
cd /path/to/streamguard/src
sudo ./streamguard -i eno1
```

Now watch YouTube on a phone/tablet on your network. You should see:

```
StreamGuard - Video Streaming Quota Enforcement
Interface: eno1 | LAN: 192.168.0.0/255.255.255.0 | Quota: 3600 sec | Mode: DRY-RUN

[STREAMING] QUIC.YouTube | 192.168.0.45:52341 -> 142.250.80.46:443
[FLOW_END] QUIC.YouTube | 192.168.0.45:52341 -> 142.250.80.46:443 | 2.1 MB | 23 sec
[QUOTA] 192.168.0.45 | total: 23/3600 seconds (0.4 min, 1%)
```

Press `Ctrl+C` to stop.

---

## Step 5: Enable Enforcement

Once detection works, enable blocking:

```bash
sudo ./streamguard -i eno1 -e -q 3600
```

Options:
- `-e` - Enable enforcement (actually blocks clients)
- `-q 3600` - 1 hour daily quota (3600 seconds)

When a client exceeds quota:

```
[QUOTA] 192.168.0.45 | total: 3612/3600 seconds (60.2 min, 100%)
[BLOCKED] 192.168.0.45 (quota exceeded: 3612/3600 seconds)
```

### Verify Blocking on Router

```bash
ssh root@192.168.0.1 'nft list set inet fw4 blocked_streaming_clients'
```

Should show the blocked IP:
```
set blocked_streaming_clients {
    type ipv4_addr
    flags timeout
    elements = { 192.168.0.45 timeout 23h59m50s }
}
```

---

## Step 6: Install as Systemd Service

Create the service for automatic startup:

```bash
sudo cp /path/to/streamguard/scripts/streamguard.service /etc/systemd/system/
```

Edit the service file to match your setup:

```bash
sudo nano /etc/systemd/system/streamguard.service
```

Update `ExecStart` line:
```ini
ExecStart=/usr/local/bin/streamguard -i eno1 -e -q 3600 -f /var/lib/streamguard/state.json
```

Enable and start:

```bash
sudo systemctl daemon-reload
sudo systemctl enable streamguard
sudo systemctl start streamguard
```

Check status:

```bash
sudo systemctl status streamguard
```

---

## Step 7: Monitoring

### View Logs

```bash
# Follow logs in real-time
sudo journalctl -u streamguard -f

# Last 100 lines
sudo journalctl -u streamguard -n 100
```

### Check Quotas

The state file shows current usage:

```bash
cat /var/lib/streamguard/state.json
```

```json
{
  "clients": [
    {"ip": "192.168.0.45", "streaming_seconds": 2847, "last_reset_date": "2026-01-05", "is_blocked": false}
  ]
}
```

### Manual Operations

```bash
# Unblock a client immediately
ssh root@192.168.0.1 "nft delete element inet fw4 blocked_streaming_clients '{ 192.168.0.45 }'"

# Check what streaming IPs are being tracked
ssh root@192.168.0.1 'nft list set inet fw4 streaming_destinations'

# Force quota reset (edit state file)
sudo systemctl stop streamguard
# Edit /var/lib/streamguard/state.json - set streaming_seconds to 0
sudo systemctl start streamguard
```

---

## Troubleshooting

### No Detection Output

1. **Check interface name**: `ip link show` to see available interfaces
2. **Check you're running as root**: `sudo ./streamguard -i eno1`
3. **Verify traffic is flowing**: `sudo tcpdump -i eno1 port 443 -c 5`

### Blocking Not Working

1. **Check enforce mode**: Look for `Mode: ENFORCE` in startup message
2. **Check SSH access**: `ssh root@192.168.0.1 "echo OK"`
3. **Check nftables sets exist**: `ssh root@192.168.0.1 'nft list sets inet fw4'`
4. **Check dnsmasq populated the set**: `ssh root@192.168.0.1 'nft list set inet fw4 streaming_destinations'`

### State File Issues

If quotas aren't persisting:

```bash
# Check directory exists
sudo mkdir -p /var/lib/streamguard

# Check permissions
sudo chown root:root /var/lib/streamguard
```

### Reset Everything

```bash
# Stop service
sudo systemctl stop streamguard

# Clear state
sudo rm /var/lib/streamguard/state.json

# Clear blocked clients on router
ssh root@192.168.0.1 'nft flush set inet fw4 blocked_streaming_clients'

# Restart
sudo systemctl start streamguard
```

---

## Configuration Reference

### StreamGuard Options

| Option | Default | Description |
|--------|---------|-------------|
| `-i <iface>` | (required) | Network interface |
| `-s <subnet>` | 192.168.0.0 | LAN subnet |
| `-m <mask>` | 255.255.255.0 | LAN netmask |
| `-q <seconds>` | 3600 | Daily quota per client |
| `-f <file>` | streamguard_state.json | State file path |
| `-e` | off | Enable enforcement mode |

### Per-Client Different Quotas

Currently StreamGuard uses one quota for all clients. For per-client quotas, you'd need to modify the source code or run multiple instances with different subnets.

### Adding More Streaming Domains

Edit `/etc/dnsmasq.d/dnsmasq-streaming.conf` on the router:

```
nftset=/newservice.com/4#inet#fw4#streaming_destinations
```

Then restart dnsmasq:
```bash
ssh root@192.168.0.1 'service dnsmasq restart'
```
