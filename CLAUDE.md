# CLAUDE.md

Developer reference for Claude Code when working with this repository.

## Project Overview

**StreamGuard** - Real-time video streaming detection and daily quota enforcement for home networks.

Uses **nDPI** (Deep Packet Inspection) to identify streaming protocols (YouTube, Netflix, TikTok, etc.) and blocks clients via **nftables** when they exceed their daily quota.

## Architecture

```
Ubuntu Machine (StreamGuard)              OpenWrt Router
┌─────────────────────────────┐           ┌──────────────────────────────┐
│  streamguard                │           │  dnsmasq                     │
│  - libpcap capture          │    SSH    │  - populates streaming IPs   │
│  - nDPI protocol detection  │ ────────► │                              │
│  - quota tracking           │   nft     │  nftables                    │
│  - state persistence        │  commands │  - blocked_streaming_clients │
└─────────────────────────────┘           │  - streaming_destinations    │
         ▲                                │  - DROP rule                 │
         │ packets                        └──────────────────────────────┘
         │
    Network (eno1)
```

**How it works:**
1. StreamGuard captures packets on Ubuntu machine
2. nDPI identifies streaming protocols (YouTube, Netflix, etc.)
3. Per-client watch time accumulated, saved to JSON
4. When quota exceeded, client IP added to router's nftables blocked set
5. Router drops traffic from blocked clients to streaming destinations
6. Quotas reset daily at midnight

## Key Files

| File | Purpose |
|------|---------|
| `src/streamguard.c` | Main C implementation (~700 lines) |
| `src/Makefile` | Build system |
| `scripts/openwrt/setup-nftables.sh` | Router firewall setup |
| `scripts/openwrt/dnsmasq-streaming.conf` | Streaming domain list |
| `scripts/openwrt/install.sh` | Deploy config to router |
| `scripts/streamguard.service` | Systemd service file |

## Common Commands

### Build
```bash
cd src
make
```

### Run (dry-run mode - logs only)
```bash
sudo ./streamguard -i eno1
```

### Run (enforcement mode - blocks when quota exceeded)
```bash
sudo ./streamguard -i eno1 -e -q 3600
```

### Deploy router config
```bash
cd scripts/openwrt
./install.sh 192.168.0.1
```

### Check blocked clients on router
```bash
ssh root@192.168.0.1 'nft list set inet fw4 blocked_streaming_clients'
```

### Manually unblock a client
```bash
ssh root@192.168.0.1 "nft delete element inet fw4 blocked_streaming_clients '{ 192.168.0.10 }'"
```

## CLI Options

```
Usage: streamguard -i <interface> [options]

Required:
  -i <iface>   Network interface (e.g., eno1, eth0)

Options:
  -s <subnet>  LAN subnet (default: 192.168.0.0)
  -m <mask>    LAN netmask (default: 255.255.255.0)
  -q <secs>    Daily quota in seconds (default: 3600)
  -f <file>    State file path (default: streamguard_state.json)
  -e           Enable enforcement mode (blocks via nftables)
  -h           Show help

Without -e, runs in dry-run mode (logs only, no blocking).
```

## Detected Protocols

StreamGuard detects these streaming services via nDPI:

**Video Streaming:**
- YouTube, Netflix, TikTok, Twitch
- Disney+, Amazon Video, Hulu
- Instagram, Facebook video

**Not Tracked (by design):**
- Spotify, Apple Music (audio only)

## Configuration

### Quota
Default: 3600 seconds (1 hour). Change with `-q`:
```bash
sudo ./streamguard -i eno1 -e -q 7200  # 2 hours
```

### LAN Subnet
Default: 192.168.0.0/24. Change with `-s` and `-m`:
```bash
sudo ./streamguard -i eno1 -s 192.168.1.0 -m 255.255.255.0
```

### State File
Default: `streamguard_state.json` in current directory. Change with `-f`:
```bash
sudo ./streamguard -i eno1 -f /var/lib/streamguard/state.json
```

## Dependencies

### Ubuntu
```bash
sudo apt install libndpi-dev libpcap-dev libcjson-dev build-essential
```

### OpenWrt
```bash
opkg install tcpdump
```

## Log Output

```
[STREAMING] QUIC.YouTube | 192.168.0.10:54321 -> 142.250.1.1:443
[FLOW_END] QUIC.YouTube | 192.168.0.10:54321 -> 142.250.1.1:443 | 5.2 MB | 45 sec
[QUOTA] 192.168.0.10 | total: 2845/3600 seconds (47.4 min, 79%)
[BLOCKED] 192.168.0.10 (quota exceeded: 3612/3600 seconds)
[RESET] 192.168.0.10: 3612 seconds -> 0 (new day: 2026-01-06)
```

## Legacy Code (Deprecated)

The `python/` directory contains deprecated approaches:
- `python/naive_*.py` - Throughput-based detection (replaced by nDPI)
- `python/scapy_sniffer.py` - ML-based classification (has buffering accuracy issues)

**Use the C implementation (`src/streamguard.c`) for production.**
