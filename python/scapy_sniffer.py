# usage with remote router openwrt
# ssh root@router 'tcpdump -i br-lan -s 1024 -U -w - port 80 or port 443' | tee capture.pcap | python3 scapy_sniffer.py
# or using local machine for simplicity
# sudo tcpdump -i wlp2s0 -s 1024 -w - port 80 or port 443 | python3 scapy_sniffer.py

from scapy.all import PcapReader, IP, TCP, UDP
import sys
import datetime 
import argparse

def process_packet(packet, training):
    if packet.haslayer(IP):
        ip_layer = packet[IP]
        original_size = ip_layer.len  # Original packet size from IP header
        identification = ip_layer.id
        ttl = ip_layer.ttl
        ip_flags = ip_layer.flags if ip_layer.flags else "NONE"

        # Initialize TCP/UDP fields
        tcp_sport, tcp_dport, tcp_seq, tcp_ack, tcp_flags = -1, -1, -1, -1, "NONE"
        udp_sport, udp_dport = -1, -1

        if packet.haslayer(TCP):
            tcp_layer = packet[TCP]
            tcp_sport, tcp_dport = tcp_layer.sport, tcp_layer.dport
            tcp_seq = tcp_layer.seq
            tcp_ack = tcp_layer.ack
            tcp_flags = tcp_layer.flags if tcp_layer.flags else "NONE"

        if packet.haslayer(UDP):
            udp_layer = packet[UDP]
            udp_sport, udp_dport = udp_layer.sport, udp_layer.dport

        if training:
            line = ( f"{ip_layer.src:>15} {ip_layer.dst:>15} {original_size:6d} "
                     f"{tcp_sport:6d} {tcp_dport:6d} {udp_sport:6d} {udp_dport:6d} "
                     f"{packet.time:18.6f} {identification:5d} {ttl:3d} "
                     f"{str(ip_flags):>4} {tcp_seq:10d} {tcp_ack:10d} {str(tcp_flags):>4} ")
            print(line) # stdout -> for training file
            print(line, file=sys.stderr) # for debugging
        else:                
            print(f"packet ips src {ip_layer.src:>15} dst {ip_layer.dst:>15} size {original_size:6d} " 
                f"tcp sport {tcp_sport:6d} dport {tcp_dport:6d} udp sport {udp_sport:6d} dport {udp_dport:6d} "
                f"time {packet.time:18.6f} "
                f"ID:{identification:5d} TTL:{ttl:3d} "
                f"IP Flags:{str(ip_flags)} TCP Seq:{tcp_seq:10d} TCP Ack:{tcp_ack:10d} TCP Flags:{str(tcp_flags)}"
                f"hour {datetime.datetime.fromtimestamp(float(packet.time)).isoformat()}")        
            

def main():
    parser = argparse.ArgumentParser(description="network traffic packet metadata sniffer")
    parser.add_argument("--train", action="store_true", help="Print metadata for creating training samples")
    args = parser.parse_args()

    with PcapReader(sys.stdin.buffer) as pcap_reader:  # Use buffer to handle binary data
        for packet in pcap_reader:
            process_packet(packet, args.train)

if __name__ == "__main__":
    main()
