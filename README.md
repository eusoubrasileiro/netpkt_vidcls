# StreamGuard

Enforce daily video streaming quotas on your home network. When kids hit their YouTube/Netflix/TikTok limit, streaming is blocked until the next day.

## How It Works

StreamGuard runs on a Linux machine (Ubuntu, Orange Pi, etc.) and monitors network traffic using **nDPI** to identify streaming protocols. When a client exceeds their daily quota, their IP is blocked from streaming destinations via your OpenWrt router's firewall.

```
Kids' Devices ──► OpenWrt Router ──► Internet
                       │
                       ▼
                 StreamGuard (Ubuntu)
                 - Detects YouTube, Netflix, TikTok...
                 - Tracks watch time per device
                 - Blocks when quota exceeded
```

## Quick Start

### Prerequisites

**Ubuntu machine:**
```bash
sudo apt install libndpi-dev libpcap-dev libcjson-dev build-essential
```

> **Note:** For best detection (modern QUIC/YouTube), consider building nDPI from source - see [CLAUDE.md](CLAUDE.md#upgrading-ndpi-recommended).

**OpenWrt router:**
- Root SSH access
- tcpdump: `opkg install tcpdump`

### 1. Build

```bash
git clone https://github.com/yourusername/streamguard.git
cd streamguard/src
make
```

### 2. Setup SSH Key Auth to Router

```bash
ssh-copy-id root@192.168.0.1
ssh root@192.168.0.1 "echo OK"  # Should work without password
```

### 3. Configure Router

```bash
cd scripts/openwrt
./install.sh 192.168.0.1
```

### 4. Test (Dry-Run Mode)

```bash
sudo ./streamguard -i eno1  # Replace eno1 with your interface
```

Watch YouTube on a device - you should see detection logs.

### 5. Enable Enforcement

```bash
sudo ./streamguard -i eno1 -e -q 3600  # 1 hour quota
```

## Usage

```
streamguard -i <interface> [options]

Options:
  -i <iface>   Network interface (required)
  -q <secs>    Daily quota in seconds (default: 3600 = 1 hour)
  -e           Enable enforcement (blocks when quota exceeded)
  -f <file>    State file path
  -h           Help
```

## Detection

StreamGuard uses nDPI's **protocol categories** to detect streaming:
- `VIDEO` - YouTube, Netflix, TikTok, etc.
- `STREAMING` - Live streaming services
- `MEDIA` - General media content

Only flows with **>100KB/s throughput** are counted (filters out static pages/thumbnails).

**Detected Services:** YouTube, Netflix, TikTok, Twitch, Disney+, Amazon Video, Hulu, Instagram, Facebook

## Run as Service

```bash
sudo cp scripts/streamguard.service /etc/systemd/system/
# Edit the service file to set your interface and quota
sudo systemctl daemon-reload
sudo systemctl enable --now streamguard
```

## Monitoring

```bash
# Check service status
sudo systemctl status streamguard

# View logs
sudo journalctl -u streamguard -f

# Check blocked clients on router
ssh root@192.168.0.1 'nft list set inet fw4 blocked_streaming_clients'

# Manually unblock someone
ssh root@192.168.0.1 "nft delete element inet fw4 blocked_streaming_clients '{ 192.168.0.10 }'"
```

## Documentation

- [DEPLOYMENT.md](DEPLOYMENT.md) - Detailed setup guide
- [CLAUDE.md](CLAUDE.md) - Developer reference

## License

MIT
