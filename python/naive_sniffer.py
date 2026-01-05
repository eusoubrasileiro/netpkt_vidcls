#!/usr/bin/env python3
"""
Naive video streaming detector - packet sniffer integration.

Reads packets from tcpdump (via stdin or named pipe) and feeds them
to the NaiveTracker for throughput-based streaming detection.

Usage:
    # Local interface (testing)
    sudo tcpdump -i eno1 -s 192 -w - port 80 or port 443 | python3 naive_sniffer.py --verbose

    # From Orange Pi 5 to OpenWrt router
    OPENWRT_IP=192.168.0.1
    ssh -o ServerAliveInterval=30 root@$OPENWRT_IP \\
        "tcpdump -i br-lan -s 192 -nn -w - 'port 80 or port 443'" \\
    | python3 naive_sniffer.py --log-only --verbose

Phase 1: Run with --log-only for 24h to validate before enforcement.
"""
import argparse
import os
import sys
import signal
import struct
import socket
from datetime import datetime
from scapy.all import PcapReader, IP

from naive_config import NAIVE_CONFIG
from naive_tracker import NaiveTracker


def ip_to_int(ip: str) -> int:
    """Convert IP string to integer for subnet comparison."""
    return struct.unpack("!I", socket.inet_aton(ip))[0]


def is_ip_in_subnet(ip: str, subnet: str, mask_bits: int) -> bool:
    """Check if IP is in given subnet."""
    subnet_int = ip_to_int(subnet)
    mask = (0xFFFFFFFF << (32 - mask_bits)) & 0xFFFFFFFF
    ip_int = ip_to_int(ip)
    return (ip_int & mask) == (subnet_int & mask)


def process_packet(packet, config: dict) -> dict | None:
    """
    Process a single packet and extract relevant fields.

    Returns dict with client_ip, server_ip, packet_size, timestamp
    or None if packet should be ignored.
    """
    if not packet.haslayer(IP):
        return None

    ip_layer = packet[IP]
    src_ip = ip_layer.src
    dst_ip = ip_layer.dst
    packet_size = ip_layer.len
    timestamp = datetime.fromtimestamp(float(packet.time))

    # Check if IPs are in LAN subnet
    src_in_lan = is_ip_in_subnet(src_ip, config['lan_subnet'], config['lan_subnet_mask'])
    dst_in_lan = is_ip_in_subnet(dst_ip, config['lan_subnet'], config['lan_subnet_mask'])

    # Ignore internal LAN traffic
    if src_in_lan and dst_in_lan:
        return None

    # Ignore traffic that's neither from nor to LAN (shouldn't happen but be safe)
    if not src_in_lan and not dst_in_lan:
        return None

    # Determine client (LAN) and server (WAN)
    if src_in_lan:
        client_ip = src_ip
        server_ip = dst_ip
    else:
        client_ip = dst_ip
        server_ip = src_ip

    return {
        'client_ip': client_ip,
        'server_ip': server_ip,
        'packet_size': packet_size,
        'timestamp': timestamp,
    }


def get_pcap_reader(source: str):
    """Get PcapReader for the specified source."""
    if source == "stdin":
        return PcapReader(sys.stdin.buffer)
    elif os.path.exists(source):
        return PcapReader(source)
    else:
        raise FileNotFoundError(f"Source '{source}' does not exist.")


def main():
    parser = argparse.ArgumentParser(
        description="Naive video streaming detector - throughput-based tracking.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Test locally (logging only)
  sudo tcpdump -i eno1 -s 192 -w - port 80 or port 443 | python3 %(prog)s --log-only --verbose

  # Production (logging only - Phase 1)
  ssh root@router "tcpdump -i br-lan -s 192 -nn -w - 'port 80 or port 443'" | python3 %(prog)s --log-only

  # Production with enforcement (Phase 2)
  ssh root@router "tcpdump ..." | python3 %(prog)s --enforce
        """
    )

    parser.add_argument(
        "--log-only", action="store_true", default=True,
        help="Only log detections, don't enforce blocks (default: True)"
    )
    parser.add_argument(
        "--enforce", action="store_true",
        help="Enable enforcement (blocks clients when quota exceeded)"
    )
    parser.add_argument(
        "--verbose", action="store_true",
        help="Enable verbose output (per-flow logging)"
    )
    parser.add_argument(
        "--source", type=str, default="stdin",
        help="Input source: 'stdin' or path to pcap/named pipe"
    )
    parser.add_argument(
        "--load-state", action="store_true",
        help="Load saved state from previous run"
    )

    args = parser.parse_args()

    # --enforce overrides --log-only
    log_only = not args.enforce

    print(f"Naive Video Streaming Detector")
    print(f"=" * 40)
    print(f"Mode: {'LOGGING ONLY' if log_only else 'ENFORCEMENT ENABLED'}")
    print(f"Verbose: {args.verbose}")
    print(f"Source: {args.source}")
    print(f"Window size: {NAIVE_CONFIG['window_size']}s")
    print(f"Rate threshold: {NAIVE_CONFIG['rate_threshold'] * 8 / 1000:.0f} kbps")
    print(f"Consecutive windows: {NAIVE_CONFIG['consecutive_windows']}")
    print(f"Daily quota: {NAIVE_CONFIG['daily_quota_seconds']}s")
    print(f"=" * 40)

    # Verify source exists
    if args.source != "stdin" and not os.path.exists(args.source):
        print(f"Error: Source '{args.source}' does not exist.", file=sys.stderr)
        sys.exit(1)

    # Initialize tracker
    tracker = NaiveTracker(
        config=NAIVE_CONFIG,
        log_only=log_only,
        verbose=args.verbose
    )

    # Load previous state if requested
    if args.load_state:
        tracker.load_state()

    # Handle graceful shutdown
    def signal_handler(signum, frame):
        print("\nShutting down...")
        tracker.flush()
        tracker.save_state()
        print("State saved. Goodbye!")
        sys.exit(0)

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    # Process packets
    print("Waiting for packets...")
    packet_count = 0

    try:
        with get_pcap_reader(args.source) as pcap_reader:
            for packet in pcap_reader:
                packet_data = process_packet(packet, NAIVE_CONFIG)

                if packet_data:
                    tracker.add_packet(
                        client_ip=packet_data['client_ip'],
                        server_ip=packet_data['server_ip'],
                        packet_size=packet_data['packet_size'],
                        timestamp=packet_data['timestamp'],
                    )
                    packet_count += 1

    except KeyboardInterrupt:
        pass
    except EOFError:
        print("Input stream ended.")
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        raise

    # Final cleanup
    tracker.flush()
    tracker.save_state()

    print(f"\nProcessed {packet_count} packets.")
    print("Final statistics:")
    for client_ip, stats in tracker.get_all_stats().items():
        print(f"  {client_ip}: {stats['watch_time_seconds']:.1f}s watched "
              f"({stats['quota_percent']:.1f}% of quota)")


if __name__ == "__main__":
    main()
