# CLAUDE.md

Developer reference for Claude Code when working with this repository.

## Project Overview

**StreamGuard** - Real-time video streaming detection and daily quota enforcement for home networks.

Uses **nDPI** (Deep Packet Inspection) to identify streaming protocols and blocks clients via **nftables** (fw4) or **iptables/ipset** (fw3) when they exceed their daily quota.

## Architecture

```
Ubuntu Machine (StreamGuard)              OpenWrt Router
+--------------------------+              +----------------------------+
|  streamguard             |              |  rpcapd (port 2002)        |
|  - rpcap:// capture      |<-- RPCAP ----+  - remote capture daemon   |
|  - nDPI detection        |              |                            |
|  - quota tracking        |              |  nftables (fw4) or         |
|                          |--- SSH ----->|  iptables/ipset (fw3)      |
+--------------------------+              +----------------------------+
```

**Flow:**
1. Router runs rpcapd, capturing LAN traffic (br-lan)
2. StreamGuard connects via `rpcap://router/br-lan`
3. nDPI detects VIDEO/STREAMING/MEDIA categories + social media
4. Session-based time tracking (45s inactivity timeout)
5. Quota exceeded -> client IP blocked via SSH to router firewall
6. Quotas reset daily at midnight

## Key Files

| File | Purpose |
|------|---------|
| `src/streamguard.c` | Main implementation |
| `src/Makefile` | Build system (auto-detects local nDPI/libpcap) |
| `scripts/openwrt/install.sh` | Deploy to router (firewall + rpcapd) |
| `scripts/streamguard.service` | Systemd service (Ubuntu) |
| `scripts/web/app.py` | Web dashboard (Flask) |
| `scripts/streamguard-web.service` | Web interface systemd service |

## Build and Run

```bash
# Build
cd src && make

# Deploy to router (one-time)
./scripts/openwrt/install.sh 192.168.0.1

# Run (dry-run)
sudo ./streamguard -r rpcap://192.168.0.1/br-lan

# Run (enforcement)
sudo ./streamguard -r rpcap://192.168.0.1/br-lan -e -R 192.168.0.1 -k ~/.ssh/id_rsa

# Replay pcap file
./streamguard -r capture.pcap
```

## CLI Options

```
Usage: streamguard -r <source> [options]
   or: streamguard -i <interface> [options]

Input (one required):
  -r <source>  rpcap:// URL, pcap file, or stdin ('-')
  -i <iface>   Live capture from local interface

Options:
  -s <subnet>  LAN subnet (default: 192.168.0.0)
  -m <mask>    LAN netmask (default: 255.255.255.0)
  -q <secs>    Daily quota in seconds (default: 3600)
  -f <file>    State file for persistence (disabled by default)
  -R <router>  Router IP for SSH-based blocking
  -u <user>    SSH user (default: root)
  -k <keyfile> SSH key file
  -F <type>    'nft' (default) or 'ipset'
  -L <file>    Log to file
  -e           Enable enforcement (blocks clients)
  -V           Video-only mode (skip social media)
  -d           Debug mode
```

**Examples:**
```bash
# OpenWrt 22.03+ (nftables)
sudo ./streamguard -r rpcap://192.168.0.1/br-lan -e -R 192.168.0.1

# OpenWrt 21.02 (iptables/ipset)
sudo ./streamguard -r rpcap://192.168.0.1/br-lan -e -R 192.168.0.1 -F ipset
```

## Tracking Modes

| Mode | Flag | Tracks |
|------|------|--------|
| ALL-SOCIAL (default) | none | Video streaming + Instagram/Facebook |
| VIDEO-ONLY | `-V` | Only YouTube, Netflix, TikTok, etc. |

## Dependencies

### Ubuntu (build)
```bash
sudo apt install build-essential libpcap-dev libcjson-dev libgcrypt20-dev
```

### nDPI 5.0 (required - Ubuntu package is outdated)
```bash
git clone --branch 5.0 --depth 1 https://github.com/ntop/nDPI.git nDPI
cd nDPI && ./autogen.sh && ./configure --with-local-libgcrypt && make
sudo make install && sudo ldconfig
```

### libpcap with remote support (for rpcap://)
```bash
git clone --depth 1 https://github.com/the-tcpdump-group/libpcap.git libpcap
cd libpcap && ./autogen.sh && ./configure --enable-remote && make
```

The Makefile auto-detects local `nDPI/` and `libpcap/` builds.

### OpenWrt
```bash
opkg update && opkg install rpcapd
# For fw3: also install ipset iptables-mod-ipset kmod-ipt-ipset
```

## SSH Setup

StreamGuard runs with sudo, so root needs SSH access to the router:

```bash
# Accept host key
sudo ssh -i ~/.ssh/id_rsa root@192.168.0.1 "echo ok"

# Run with key
sudo ./streamguard -r rpcap://... -e -R 192.168.0.1 -k ~/.ssh/id_rsa
```

## Router Commands

```bash
# Check blocked clients
ssh root@192.168.0.1 'nft list set inet fw4 blocked_streaming_clients'
# or for ipset:
ssh root@192.168.0.1 'ipset list blocked_streaming_clients'

# Unblock client
ssh root@192.168.0.1 "nft delete element inet fw4 blocked_streaming_clients '{ 192.168.0.10 }'"
# or:
ssh root@192.168.0.1 "ipset del blocked_streaming_clients 192.168.0.10"

# Check rpcapd status
ssh root@192.168.0.1 '/etc/init.d/rpcapd status'
```

## Log Output

```
14:32:15 INFO  StreamGuard - Video Streaming Quota Enforcement
14:32:16 INFO  STREAMING: QUIC.YouTube (UDP/Video) | 192.168.0.10 -> 142.250.1.1
14:32:16 INFO  SESSION_START: 192.168.0.10
14:35:20 INFO  SESSION_END: 192.168.0.10 | +120 sec | total: 2845/3600 sec (79%)
14:40:00 WARN  BLOCKED: 192.168.0.10 (quota exceeded)
00:00:01 INFO  RESET: 192.168.0.10 (new day)
```

Use `-d` for debug output, `-L <file>` to log to file.

## Testing

```bash
cd src
make test          # All tests
make test-unit     # Unit tests only
```

## Web Interface

A Flask-based web dashboard for viewing client quota status.

### Features
- Real-time quota dashboard at `http://localhost:8080`
- Progress bars with color coding (green/yellow/red)
- JSON API for integration

### Installation

```bash
# Install Flask
pip3 install flask

# Copy web app
sudo mkdir -p /usr/local/lib/streamguard-web
sudo cp scripts/web/app.py /usr/local/lib/streamguard-web/

# Install and start service
sudo cp scripts/streamguard-web.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now streamguard-web
```

### Configuration

Environment variables (set in systemd service):

| Variable | Default | Description |
|----------|---------|-------------|
| `STREAMGUARD_STATE_FILE` | `/var/lib/streamguard/state.json` | Path to state file |
| `STREAMGUARD_QUOTA` | `3600` | Daily quota in seconds |
| `PORT` | `8080` | Web server port |

### API Endpoints

| Endpoint | Description |
|----------|-------------|
| `GET /` | HTML dashboard |
| `GET /api/clients` | JSON list of all clients with quota info |
| `GET /api/clients/<ip>` | JSON data for specific client |

**Example API response:**
```json
{
  "clients": [
    {
      "ip": "192.168.0.25",
      "streaming_seconds": 1234,
      "quota_seconds": 1800,
      "remaining_seconds": 566,
      "percentage_used": 68.6,
      "is_blocked": false,
      "color": "yellow"
    }
  ],
  "quota_seconds": 1800,
  "timestamp": "2026-01-13T16:50:00"
}
```

## Known Issues

1. **nDPI Ubuntu package (4.2.0)** - Does not detect QUIC/YouTube properly. Build nDPI 5.0 from source.
2. **Social media** - Only Instagram/Facebook tracked. Twitter/Snapchat not implemented.
