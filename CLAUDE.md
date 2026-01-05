# CLAUDE.md

Developer reference for Claude Code when working with this repository.

## Project Overview

**StreamGuard** - Real-time video streaming detection and daily quota enforcement for home networks.

Uses **nDPI** (Deep Packet Inspection) to identify streaming protocols and blocks clients via **nftables** when they exceed their daily quota.

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
2. nDPI detects protocol and determines its category (VIDEO, STREAMING, MEDIA)
3. Only flows with throughput >100KB/s are counted (filters out static pages)
4. Per-client watch time accumulated, saved to JSON
5. When quota exceeded, client IP added to router's nftables blocked set
6. Router drops traffic from blocked clients to streaming destinations
7. Quotas reset daily at midnight

## Detection Approach

### Category-Based Detection

Rather than hardcoding individual protocols (YouTube, Netflix, etc.), StreamGuard uses nDPI's **protocol categories**:

```c
ndpi_protocol_category_t cat = ndpi_get_proto_category(ndpi_module, proto);

return (cat == NDPI_PROTOCOL_CATEGORY_VIDEO ||
        cat == NDPI_PROTOCOL_CATEGORY_STREAMING ||
        cat == NDPI_PROTOCOL_CATEGORY_MEDIA);
```

**Benefits:**
- Future-proof: New services auto-detected by category
- No hardcoded protocol list to maintain
- Works even when nDPI doesn't have specific service detection

### Throughput Filtering

To avoid counting static pages (Facebook developer docs, YouTube homepage, etc.) as streaming:

```c
#define MIN_STREAMING_RATE 100000  /* 100KB/s minimum */

uint64_t bytes_per_sec = (f->bytes * 1000) / duration_ms;
if (bytes_per_sec < MIN_STREAMING_RATE) {
    /* Skip - not actual video playback */
}
```

A 720p video uses ~2-5 Mbps (250-625 KB/s). 100KB/s is a conservative threshold.

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

## Detected Categories

StreamGuard detects traffic in these nDPI categories:

- **NDPI_PROTOCOL_CATEGORY_VIDEO** - YouTube, Netflix, TikTok, etc.
- **NDPI_PROTOCOL_CATEGORY_STREAMING** - Live streaming services
- **NDPI_PROTOCOL_CATEGORY_MEDIA** - General media content

Social media with video (Instagram, Facebook) is also detected when actively streaming video content.

**Not Tracked (by design):**
- Audio-only services (Spotify, Apple Music) - different category
- Low-throughput browsing (<100KB/s) on video sites

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

### Upgrading nDPI (Recommended)

The Ubuntu package (libndpi-dev) may be outdated and miss modern QUIC/YouTube detection. For best results, build nDPI from source:

```bash
# Remove old package
sudo apt remove libndpi-dev

# Build from source
git clone https://github.com/ntop/nDPI.git
cd nDPI
./autogen.sh
./configure
make
sudo make install
sudo ldconfig
```

Then rebuild StreamGuard.

## Log Output

```
[STREAMING] QUIC.YouTube | 192.168.0.10:54321 -> 142.250.1.1:443
[FLOW_END] QUIC.YouTube | 192.168.0.10:54321 -> 142.250.1.1:443 | 5.2 MB | 45 sec | 118 KB/s
[QUOTA] 192.168.0.10 | total: 2845/3600 seconds (47.4 min, 79%)
[SKIP] Low throughput flow (12 KB/s < 100 KB/s), not counting
[BLOCKED] 192.168.0.10 (quota exceeded: 3612/3600 seconds)
[RESET] 192.168.0.10: 3612 seconds -> 0 (new day: 2026-01-06)
```

## Known Issues

1. **nDPI 4.2.0 (Ubuntu package)** - May not detect modern QUIC/YouTube properly. Build nDPI from source for better detection.

2. **Flow duration at shutdown** - Fixed. Previously showed 40+ year durations due to timestamp underflow.

3. **False positives on static pages** - Fixed. Throughput filtering now excludes low-bandwidth flows.
