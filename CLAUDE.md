# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Real-time network packet analysis to identify video streaming traffic and enforce daily time quotas. When a client exceeds their quota, the system blocks specific `(client_ip, server_ip)` pairs using nftables on an OpenWrt router.

**Two approaches:**
- **naive branch** (RECOMMENDED): Throughput-based tracking using EWMA, hysteresis, and buffer-credit accounting
- **main branch** (DEPRECATED): ML-based classification using ExtraTree model with packet header features

## ⚠️ CRITICAL: Which Approach to Use?

**USE THE NAIVE APPROACH.** The ML approach has a fundamental flaw that makes it unsuitable for quota enforcement.

### The Buffering Problem (Why ML Fails)

Modern video streaming uses adaptive bitrate (ABR) with segment-based delivery:
- **Burst phase** (2-3s): Downloads 6s of video at high speed
- **Silent phase** (3-4s): Player buffers, no network traffic
- **ML counts only burst time** → 30min video = ~10min detected ❌
- **Naive counts correctly** → Tracks buffer credit during silence ✅

### Quick Comparison

| Criterion | ML Approach | Naive Approach |
|-----------|-------------|----------------|
| **Time accuracy** | ❌ 30-40% of actual | ✅ ~95% accurate |
| **Code complexity** | ❌ 500+ lines | ✅ ~60 lines |
| **Maintenance** | ❌ Retrain model | ✅ Adjust thresholds |
| **False positives** | ✅ ~7% | ⚠️ ~20% (acceptable) |
| **Detection delay** | ⚠️ 30s | ✅ 9-15s |

**Verdict:** Naive approach is significantly better for this use case.

### Implementation Status

- **ML approach (main branch):** Fully implemented but fundamentally flawed
- **Naive approach (naive branch):** Pseudocode in README (lines 192-264), NOT YET IMPLEMENTED

**See `IMPLEMENTATION_PLAN.md` for detailed naive implementation plan.**

## Common Commands

### Running the sniffer

**Local interface:**
```bash
sudo tcpdump -i eno1 -s 1024 -w - port 80 or port 443 | python3 scapy_sniffer.py --verbose
```

**From Orange Pi 5 to OpenWrt router:**
```bash
OPENWRT_IP=192.168.0.1
ssh -o ServerAliveInterval=30 root@$OPENWRT_IP \
  "tcpdump -i br-lan -s 192 -nn -w - 'port 80 or port 443'" \
| python3 scapy_sniffer.py --verbose
```

### Training data collection

```bash
sudo tcpdump -i <interface> -s 1024 -w - port 80 or port 443 | python3 scapy_sniffer.py --train
```

Training data should be collected from single clients, manually classified scenarios (see `python/training/Readme.md` for list of existing training data).

## Architecture

### Data Flow

1. **tcpdump** captures packets (TCP/UDP ports 80, 443) from network interface
2. **scapy_sniffer.py** processes packets in 10-second windows:
   - Parses packet headers (IP, TCP, UDP fields)
   - Identifies clients (LAN IPs) and servers (WAN IPs) via subnet filtering
   - Creates statistical features from packet data
3. **ML path** (main branch): ExtraTree model classifies traffic as streaming/non-streaming
4. **Naive path** (naive branch): Tracks smoothed throughput with state machine (PLAYING/IDLE)
5. **blocker.py** manages client state, tracks usage time, enforces quotas
6. **nftables** on OpenWrt router blocks specific IP pairs when quota exceeded

### Key Components

**python/scapy_sniffer.py**
- Main entry point for packet processing
- Reads from stdin (piped from tcpdump) or named pipe
- Processes packets in 10-second windows
- `--train` flag for data collection, `--verbose` for detailed output
- Handles both training and inference modes

**python/feature_creation.py**
- `preprocess()`: Filters LAN↔WAN traffic, assigns client/server roles, converts categorical fields
- `make_windowed_features()`: Extracts 25+ statistical features per client per 10s window:
  - Upload/download speeds, packet size statistics (mean, variance, entropy)
  - TCP/UDP port diversity, unique IPs (connection multiplexing)
  - Packet timing (delay average, jitter, entropy)
  - TTL statistics (important for identifying CDN patterns)
- Selected features for model: `ack_entropy`, `ttl_entropy`, `tcp_ack_var`, `udp_nports`, `pkt_entropy`, `dw_pkt_avg`, `dw_ttl_avg`, `dw_ttl_unique`, `dw_pkt_entropy`, `dl_pkt_avg`

**python/blocker.py**
- Tracks streaming state with hysteresis (3 consecutive classifications to change state)
- Accumulates watch time per client
- When quota exceeded, progressively blocks server IPs (highest traffic first)
- Persists state to `clients.json`, writes blocked IPs to `/etc/blocked-ips-v4.txt`
- Currently commented out in scapy_sniffer.py (TODO: integrate)

**python/config.py**
- Central configuration: LAN subnet, window size (10s), class-1 threshold (0.6), streaming limit (3600s default)
- Selected features list for model
- Paths to training data (`training/raw.h5`) and model (`etree.joblib`)

**libpcap/packets.c**
- Older C implementation using libpcap (not actively used)
- Demonstrates basic packet capture and PPS calculation

### nftables Integration (OpenWrt)

Router configuration for blocking (see README for full example):
```nft
table inet fw4 {
  set stream_user_block {
    type ipv4_addr . ipv4_addr  # Concatenated set for (client . server) pairs
    flags timeout
    timeout 24h
  }
  chain stream_quota {
    type filter hook forward priority 0; policy accept;
    ip saddr . ip daddr @stream_user_block drop
  }
}
```

Python adds blocked pairs via:
```bash
nft add element inet fw4 stream_user_block '{ <client_ip> . <server_ip> timeout 2h }'
```

### ML Model Details

- **Algorithm**: ExtraTree (Extra Trees Classifier) with default parameters
- **Training**: ~185MB data, 8 manually classified scenarios (YouTube, Instagram, normal browsing, locked screen)
- **Performance**: ~90-93% binary classification accuracy
- **10-second window**: Smaller windows decrease accuracy
- **Real-time classification**: Uses 3 consecutive class-1 predictions (30s total) with 70% probability threshold to register streaming state

### Naive Approach (Current Branch)

Instead of ML, uses smoothed throughput tracking:
- **EWMA** smoothing of bytes/sec (e.g., 3s windows, alpha=0.3)
- **Hysteresis thresholds**: START_T=400kbps, STOP_T=150kbps with consecutive window requirements (K=2 to start, M=6 to stop)
- **Buffer-credit accounting**: Tracks buffered content in seconds (max 90s), counts watch time during quiet gaps until buffer depleted
- Handles adaptive bitrate (ABR) and segment-based downloads (2-6s bursts followed by silence)
- See README lines 192-264 for reference implementation

## Configuration

Edit `python/config.py`:
- `lan-subnet` / `lan-subnet-mask`: Define your LAN network
- `window-size`: Statistical analysis window (default 10s)
- `class-1-threshold`: ML probability threshold for streaming classification (default 0.6)
- `streaming-limit-seconds`: Daily quota per client (default 3600s = 1 hour)

## Dependencies

Install with pip:
```bash
pip install pandas scapy joblib numpy
```

Router requires: `tcpdump`, `nftables`

## Training Workflow

1. Collect raw packet data for different scenarios (video streaming, normal browsing, audio-only, etc.)
2. Save to CSV files with `--train` flag
3. Manually label scenarios (class 0 = not streaming, class 1 = streaming)
4. Process through `feature_creation.py` to extract statistical features
5. Train ExtraTree model, serialize to `etree.joblib`
6. Feature selection already performed (11 best features in `config['selected_features']`)

Existing training data categories in `python/training/Readme.md`:
- YouTube videos (various lengths, including shorts)
- Instagram scrolling/videos
- Normal work activities (Teams, Outlook, web browsing)
- Idle (locked screen)

Still needed: Audio streaming (Spotify, Audible) to improve negative class examples.

## Known Issues / TODOs

1. **Buffering detection**: Current ML approach only catches bursts, doesn't account for ABR buffering patterns (naive branch addresses this)
2. **blocker.py integration**: Currently commented out in scapy_sniffer.py, needs activation and testing
3. **Provider aggregation**: Should track state per (client, provider) rather than individual IPs, since CDNs rotate IPs
4. **Minimum session length**: Add filter to ignore sessions <10s (previews/thumbnails)
5. **Cooldown mechanism**: Add grace period (30s) before blocking to show warning message
6. **SNI/DNS labeling**: Optional enhancement to identify providers (YouTube, Netflix) via TLS SNI or DNS logs
7. **Feature engineering**: Current approach may need rethinking - consider per-(client, server) pair features rather than per-client aggregation

## Architecture Notes

- The system runs on separate hardware from the router (Orange Pi 5 or similar SBC) due to Python/ML requirements
- tcpdump on router forwards raw packets via SSH to SBC for processing
- Only header data captured (192-1024 bytes per packet) since payloads are encrypted
- Enforcement via router's nftables allows O(1) blocking lookups with concatenated sets
- State persistence in JSON allows script restarts without losing usage tracking
