# ML Approach (Deprecated)

> **Status:** DEPRECATED - See [BUFFERING_PROBLEM.md](BUFFERING_PROBLEM.md) for why this approach fails for quota enforcement.

This document describes the original machine learning approach on the `main` branch.

## Overview

Real-time network packet analysis to identify video streaming traffic using ML classification.

### How It Works

1. **Packet Capture:** `tcpdump` captures network traffic from the LAN interface
2. **Real-Time Analysis:** Traffic is piped to `scapy_sniffer.py` for 10-second window analysis
3. **Per-Client Classification:** ExtraTree model classifies traffic as "streaming" or "not streaming"
4. **Time Tracking:** `blocker.py` tracks cumulative streaming time per client
5. **Automated Blocking:** When quota exceeded, blocks server IPs via nftables

### Running

```bash
# Local interface
sudo tcpdump -i eno1 -s 1024 -w - port 80 or port 443 | python3 scapy_sniffer.py --verbose

# From Orange Pi 5 to OpenWrt router
OPENWRT_IP=192.168.0.1
ssh -o ServerAliveInterval=30 root@$OPENWRT_IP \
  "tcpdump -i br-lan -s 192 -nn -w - 'port 80 or port 443'" \
| python3 scapy_sniffer.py --verbose
```

## Model Details

- **Algorithm:** ExtraTree (Extra Trees Classifier) with default parameters
- **Training data:** ~185MB, 8 manually classified scenarios
- **Window size:** 10 seconds
- **Accuracy:** ~90-93% binary classification

### Features Used

Selected features (see `python/config.py`):
- `ack_entropy`, `ttl_entropy`, `tcp_ack_var`
- `udp_nports`, `pkt_entropy`
- `dw_pkt_avg`, `dw_ttl_avg`, `dw_ttl_unique`
- `dw_pkt_entropy`, `dl_pkt_avg`

### Classification Logic

- Uses 3 consecutive class-1 predictions (30s total)
- Requires 70% probability threshold to register streaming state
- Hysteresis: 3 consecutive class-0 to exit streaming state

## Training Data

See `python/training/Readme.md` for the full list. Categories:
- YouTube videos (various lengths, including shorts)
- Instagram scrolling/videos
- Normal work activities (Teams, Outlook, web browsing)
- Idle (locked screen)

**Missing:** Audio streaming (Spotify, Audible) for better negative class examples.

## Why It Fails

The fundamental problem is **ABR buffering**:

```
[BURST 2-3s] → [SILENCE 3-4s] → [repeat...]
     ↑              ↑
   detected      NOT detected
```

ML only counts the burst phase, resulting in ~30-40% of actual watch time.

**See [BUFFERING_PROBLEM.md](BUFFERING_PROBLEM.md) for full analysis.**

## Files

- `python/scapy_sniffer.py` - Main entry point
- `python/feature_creation.py` - Feature extraction (25+ features)
- `python/config.py` - Configuration
- `python/blocker.py` - State tracking and blocking
- `python/etree.joblib` - Trained model
- `python/training/raw.h5` - Training data

## Deprecation Notice

This approach is kept for reference but should not be used for quota enforcement. The naive throughput-based approach on the `naive` branch is recommended instead.
