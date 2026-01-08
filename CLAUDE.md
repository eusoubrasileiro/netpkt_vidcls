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
2. nDPI detects protocol - matches VIDEO/STREAMING/MEDIA categories + social media (Instagram, Facebook)
3. Session-based time tracking with 45s inactivity timeout
4. Per-client watch time accumulated, saved to JSON
5. When quota exceeded, client IP added to router's nftables blocked set
6. Router drops traffic from blocked clients to streaming destinations
7. Quotas reset daily at midnight

## Detection Approach

### Two Tracking Modes

StreamGuard supports two tracking modes:

| Mode | Flag | Tracks |
|------|------|--------|
| **ALL-SOCIAL** (default) | none | Video streaming + all Instagram/Facebook activity |
| **VIDEO-ONLY** | `-V` | Only pure video streaming (YouTube, Netflix, TikTok) |

### Category-Based Detection

Pure video services are detected via nDPI protocol categories:

```c
/* VIDEO/STREAMING/MEDIA categories - always tracked */
if (proto.category == NDPI_PROTOCOL_CATEGORY_VIDEO ||
    proto.category == NDPI_PROTOCOL_CATEGORY_STREAMING ||
    proto.category == NDPI_PROTOCOL_CATEGORY_MEDIA) {
    return 1;
}
```

### Social Media Detection

Instagram and Facebook are classified as `SOCIAL_NETWORK` in nDPI (not VIDEO), so they require explicit protocol detection:

```c
/* Track social media unless in video-only mode */
static int is_social_media_protocol(ndpi_protocol proto) {
    uint16_t app = proto.proto.app_protocol;
    return (app == NDPI_PROTOCOL_INSTAGRAM ||
            app == NDPI_PROTOCOL_FACEBOOK ||
            app == NDPI_PROTOCOL_FACEBOOK_REEL_STORY);
}
```

### Session-Based Timing

Time is tracked per-session with a 45-second inactivity timeout:
- Session starts when first tracked packet arrives
- Each packet resets the inactivity timer
- Session ends after 45s of no packets (handles idle browser tabs)
- Duration = (last_activity - session_start)

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
  -V           Video-only mode (ignore social media browsing)
  -d           Debug mode (verbose output)
  -h           Show help

Default: tracks all social media (Instagram, Facebook) + video streaming.
With -V: only tracks pure video streaming (YouTube, Netflix, TikTok).
Without -e, runs in dry-run mode (logs only, no blocking).
```

## What Gets Tracked

### Always Tracked (VIDEO/STREAMING/MEDIA categories)
- YouTube, Netflix, TikTok, Disney+, etc.
- Live streaming services (Twitch, etc.)
- General media content

### Tracked in Default Mode (social media protocols)
- **Instagram** - Reels, Stories, browsing, video
- **Facebook** - Reels, Stories, video, browsing
- **Facebook Messenger** video calls (if detected)

### Not Tracked
- Audio-only services (Spotify, Apple Music) - `MUSIC` category
- Other social networks (Twitter/X, Snapchat) - not yet added
- Non-media web browsing

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
StreamGuard - Video Streaming Quota Enforcement
Interface: eno1 | LAN: 192.168.0.0/255.255.255.0 | Quota: 3600 sec | ALL-SOCIAL | DRY-RUN

[STREAMING] QUIC.YouTube | 192.168.0.10:54321 -> 142.250.1.1:443
[STREAMING] TLS.Instagram | 192.168.0.10:54322 -> 157.240.1.1:443
[SESSION_START] 192.168.0.10
[SESSION_END] 192.168.0.10 | +120 sec | total: 2845/3600 sec (47.4 min, 79%)
[BLOCKED] 192.168.0.10 (quota exceeded: 3612/3600 seconds)
[RESET] 192.168.0.10: 3612 seconds -> 0 (new day: 2026-01-06)
```

## Known Issues

1. **nDPI 4.2.0 (Ubuntu package)** - Does not detect modern QUIC/YouTube. Must build nDPI 5.0 from source with `--with-local-libgcrypt`.

2. **Twitter/X, Snapchat not tracked** - Only Instagram/Facebook social media currently implemented. Can be added if needed.

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
