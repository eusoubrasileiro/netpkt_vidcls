# StreamGuard Deployment Guide

This guide covers deploying StreamGuard systemd services on Ubuntu.

## Prerequisites

### Ubuntu Dependencies

```bash
sudo apt install build-essential libpcap-dev libcjson-dev libgcrypt20-dev python3-pip
pip3 install flask
```

### nDPI 5.0 (required)

The Ubuntu package is outdated. Build from source:

```bash
git clone --branch 5.0 --depth 1 https://github.com/ntop/nDPI.git nDPI
cd nDPI && ./autogen.sh && ./configure --with-local-libgcrypt && make
sudo make install && sudo ldconfig
```

### libpcap with Remote Support

Required for `rpcap://` remote capture:

```bash
git clone --depth 1 https://github.com/the-tcpdump-group/libpcap.git libpcap
cd libpcap && ./autogen.sh && ./configure --enable-remote && make
```

### OpenWrt Router Setup

Run the install script to configure rpcapd and firewall on your router:

```bash
./scripts/openwrt/install.sh 192.168.0.1
```

---

## Building StreamGuard

```bash
cd src && make
sudo cp streamguard /usr/local/bin/
```

---

## Service 1: StreamGuard (Main Service)

The main service that captures packets and enforces quotas.

### Installation

```bash
# Copy the service file
sudo cp scripts/streamguard.service /etc/systemd/system/

# Create required directories
sudo mkdir -p /var/lib/streamguard
sudo mkdir -p /var/log/streamguard

# Reload systemd
sudo systemctl daemon-reload
```

### Configuration

Edit the service file to match your environment:

```bash
sudo systemctl edit streamguard --full
```

| Option | Description | Default |
|--------|-------------|---------|
| `-r <source>` | rpcap URL: `rpcap://<router>/<interface>` | `rpcap://192.168.0.1/br-lan` |
| `-R <router>` | Router IP for SSH blocking | `192.168.0.1` |
| `-F <type>` | Firewall type: `nft` (OpenWrt 22.03+) or `ipset` (21.02) | `ipset` |
| `-k <key>` | SSH private key for router access | `/root/.ssh/id_rsa` |
| `-q <secs>` | Daily quota in seconds | `1800` (30 min) |
| `-f <file>` | State file for persistence | `/var/lib/streamguard/state.json` |
| `-L <file>` | Log file | `/var/log/streamguard.log` |
| `-e` | Enable enforcement (required for blocking) | - |
| `-V` | Video-only mode (skip social media) | - |

### SSH Setup

Root needs SSH access to the router:

```bash
# Generate key if needed
sudo ssh-keygen -t rsa -f /root/.ssh/id_rsa -N ""

# Copy to router
sudo ssh-copy-id -i /root/.ssh/id_rsa root@192.168.0.1

# Test connection
sudo ssh -i /root/.ssh/id_rsa root@192.168.0.1 "echo ok"
```

### Start Service

```bash
sudo systemctl enable streamguard
sudo systemctl start streamguard
```

### Verify

```bash
# Check status
sudo systemctl status streamguard

# View logs
sudo journalctl -u streamguard -f

# Or check log file
sudo tail -f /var/log/streamguard.log
```

---

## Service 2: StreamGuard Web Interface

Flask-based dashboard for viewing client quota status.

### Installation

```bash
# Create directory and copy web app
sudo mkdir -p /usr/local/lib/streamguard-web
sudo cp scripts/web/app.py /usr/local/lib/streamguard-web/

# Copy the service file
sudo cp scripts/streamguard-web.service /etc/systemd/system/

# Reload systemd
sudo systemctl daemon-reload
```

### Configuration

Edit environment variables in the service file:

```bash
sudo systemctl edit streamguard-web --full
```

| Variable | Description | Default |
|----------|-------------|---------|
| `STREAMGUARD_STATE_FILE` | Path to StreamGuard state file | `/var/lib/streamguard/state.json` |
| `STREAMGUARD_QUOTA` | Daily quota in seconds | `1800` |
| `PORT` | Web server port | `8080` |

### Start Service

```bash
sudo systemctl enable streamguard-web
sudo systemctl start streamguard-web
```

### Verify

```bash
# Check status
sudo systemctl status streamguard-web

# Test web interface
curl http://localhost:8080/api/clients
```

### Access

- Dashboard: `http://<server-ip>:8080`
- API: `http://<server-ip>:8080/api/clients`

---

## Quick Deploy (All Services)

Copy and run these commands to deploy everything:

```bash
# Build and install binary
cd src && make
sudo cp streamguard /usr/local/bin/

# Install main service
sudo cp scripts/streamguard.service /etc/systemd/system/
sudo mkdir -p /var/lib/streamguard /var/log/streamguard

# Install web interface
pip3 install flask
sudo mkdir -p /usr/local/lib/streamguard-web
sudo cp scripts/web/app.py /usr/local/lib/streamguard-web/
sudo cp scripts/streamguard-web.service /etc/systemd/system/

# Start services
sudo systemctl daemon-reload
sudo systemctl enable --now streamguard streamguard-web

# Verify
sudo systemctl status streamguard streamguard-web
```

---

## Management Commands

```bash
# Start/stop/restart
sudo systemctl start streamguard
sudo systemctl stop streamguard
sudo systemctl restart streamguard

# View logs
sudo journalctl -u streamguard -f
sudo journalctl -u streamguard-web -f

# Check blocked clients on router
ssh root@192.168.0.1 'nft list set inet fw4 blocked_streaming_clients'
# or for ipset:
ssh root@192.168.0.1 'ipset list blocked_streaming_clients'

# Unblock a client
ssh root@192.168.0.1 "nft delete element inet fw4 blocked_streaming_clients '{ 192.168.0.10 }'"
# or for ipset:
ssh root@192.168.0.1 "ipset del blocked_streaming_clients 192.168.0.10"
```

---

## Troubleshooting

### StreamGuard won't start

1. Check logs: `sudo journalctl -u streamguard -e`
2. Verify rpcapd is running on router: `ssh root@192.168.0.1 '/etc/init.d/rpcapd status'`
3. Test SSH access: `sudo ssh -i /root/.ssh/id_rsa root@192.168.0.1 "echo ok"`

### Web interface shows no data

1. Ensure StreamGuard is running with `-f` flag for state file
2. Check state file exists: `ls -la /var/lib/streamguard/state.json`
3. Verify state file path matches in both services

### No clients being blocked

1. Ensure `-e` flag is present (enforcement mode)
2. Check SSH key permissions: `sudo ls -la /root/.ssh/id_rsa`
3. Verify firewall type matches router (`-F nft` or `-F ipset`)
