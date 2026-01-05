# netpkt_vidcls

Real-time video streaming detection and quota enforcement for home LANs.

## What It Does

- Detects video streaming (YouTube, Netflix, etc.) per client
- Tracks watch time with configurable daily quotas
- Blocks streaming servers via nftables when quota exceeded

## How It Works

Sniffs TCP/UDP traffic (ports 80, 443) and tracks throughput per `(client, server)` pair. When sustained high throughput is detected (>300kbps for 15s), it's classified as streaming and watch time accumulates.

On quota exceeded, blocks the `(client_ip, server_ip)` pair using an nftables concatenated set with automatic timeout expiry.

## Quick Start

### 1. Router Setup (OpenWrt)

Create `/etc/nftables.d/30-streamctl.nft`:

```nft
table inet fw4 {
  set stream_user_block {
    type ipv4_addr . ipv4_addr
    flags timeout
    timeout 24h
  }
  chain stream_quota {
    type filter hook forward priority 0; policy accept;
    ip saddr . ip daddr @stream_user_block drop
  }
}
```

Reload: `fw4 reload`

### 2. Run the Detector

```bash
# From monitoring host (Orange Pi 5, etc.)
OPENWRT_IP=192.168.0.1
ssh root@$OPENWRT_IP "tcpdump -i br-lan -s 192 -nn -w - 'port 80 or port 443'" \
  | python3 python/naive_sniffer.py --log-only --verbose
```

Use `--enforce` instead of `--log-only` to enable blocking.

## Configuration

Edit `python/naive_config.py`:

```python
'rate_threshold': 300_000,      # bytes/sec to detect streaming
'consecutive_windows': 3,       # windows (5s each) to confirm
'daily_quota_seconds': 3600,    # 1 hour default
```

## Requirements

- Python 3 + `pandas`, `scapy`
- OpenWrt router with `tcpdump` + `nftables`

## Status

| Branch | Approach | Status |
|--------|----------|--------|
| `naive` | Throughput-based | Active (recommended) |
| `main` | ML classification | Deprecated |

The ML approach has a [buffering accuracy problem](docs/BUFFERING_PROBLEM.md) that makes it unsuitable for quota enforcement.

## Documentation

- [Why Naive > ML](docs/BUFFERING_PROBLEM.md) - The buffering problem explained
- [Implementation Plan](docs/IMPLEMENTATION_PLAN.md) - Detailed implementation phases
- [ML Approach](docs/ML_APPROACH.md) - Deprecated ML documentation
