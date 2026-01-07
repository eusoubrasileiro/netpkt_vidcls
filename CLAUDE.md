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
/* nDPI 5.0: category is directly in ndpi_protocol struct */
return (proto.category == NDPI_PROTOCOL_CATEGORY_VIDEO ||
        proto.category == NDPI_PROTOCOL_CATEGORY_STREAMING ||
        proto.category == NDPI_PROTOCOL_CATEGORY_MEDIA);
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

### Run via tcpdump pipe
```bash
sudo tcpdump -i eno1 -w - | ./streamguard -r -
```

### Capture and save pcap for later testing
```bash
sudo tcpdump -i eno1 -w - | tee streaming_sample.pcap | ./streamguard -r -
```

### Replay saved pcap
```bash
./streamguard -r streaming_sample.pcap
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
   or: streamguard -r <pcap_file> [options]

Input (one required):
  -i <iface>   Capture live from network interface (e.g., eno1, eth0)
  -r <file>    Read from pcap file (use '-' for stdin pipe from tcpdump)

Options:
  -s <subnet>  LAN subnet (default: 192.168.0.0)
  -m <mask>    LAN netmask (default: 255.255.255.0)
  -q <secs>    Daily quota in seconds (default: 3600)
  -f <file>    State file path (default: streamguard_state.json)
  -e           Enable enforcement mode (blocks via nftables)
  -d           Debug mode (verbose output)
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

### Ubuntu - Build Dependencies
```bash
sudo apt install build-essential git autoconf automake libtool pkg-config \
    libpcap-dev libcjson-dev libgcrypt20-dev libgpg-error-dev \
    flex bison libjson-c-dev libnuma-dev libpcre2-dev libmaxminddb-dev librrd-dev
```

### OpenWrt
```bash
opkg install tcpdump
```

### Building nDPI 5.0 (Required)

The Ubuntu package (libndpi-dev 4.2.0) is outdated. StreamGuard requires nDPI 5.0 built from source with libgcrypt support for proper QUIC/YouTube detection.

```bash
# Clone nDPI 5.0 into the project
cd /path/to/streamguard
git clone --branch 5.0 --depth 1 https://github.com/ntop/nDPI.git nDPI

# Build with libgcrypt support
cd nDPI
./autogen.sh
./configure --with-local-libgcrypt
make -j$(nproc)
sudo make install
sudo ldconfig
```

The Makefile automatically uses the local `nDPI/` build if present.

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

1. **nDPI 4.2.0 (Ubuntu package)** - Does not detect modern QUIC/YouTube. Must build nDPI 5.0 from source with `--with-local-libgcrypt`.

2. **Flow duration at shutdown** - Fixed. Previously showed 40+ year durations due to timestamp underflow.

3. **False positives on static pages** - Fixed. Throughput filtering now excludes low-bandwidth flows.

## nDPI 5.0 API Notes

StreamGuard uses nDPI 5.0 API which differs from older versions:

```c
/* Initialization - no bitmask needed, all protocols enabled by default */
ndpi_module = ndpi_init_detection_module(NULL);
ndpi_finalize_initialization(ndpi_module);

/* Detection - extra input_info parameter */
ndpi_detection_process_packet(ndpi_module, flow, packet, len, time_ms, NULL);

/* Giveup - only 2 parameters */
ndpi_detection_giveup(ndpi_module, flow);

/* Protocol access - nested struct */
flow->detected_protocol.proto.app_protocol
flow->detected_protocol.category  /* category directly accessible */
```
