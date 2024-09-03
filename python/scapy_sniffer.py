# using remote router openwrt
# ssh root@router 'tcpdump -i br-lan -s 1024 -U -w - port 80 or port 443' | tee capture.pcap | python3 scapy_sniffer.py
# or using local machine for simplicity
# sudo tcpdump -i wlp2s0 -s 1024 -w - port 80 or port 443 | python3 scapy_sniffer.py

from scapy.all import PcapReader, IP, TCP
import sys
import datetime 

def process_packet(packet):
    if packet.haslayer(IP):
        ip_layer = packet[IP]
        original_size = ip_layer.len  # Original packet size from IP header
        print(f"IP {ip_layer.src} -> {ip_layer.dst} Packet Size: {original_size}" 
              f" Time: {datetime.datetime.fromtimestamp(float(packet.time))}")        
        # if packet.haslayer(TCP):
        #     tcp_layer = packet[TCP]
        #     print(f"Port {tcp_layer.sport} -> {tcp_layer.dport}")
        

def main():
    with PcapReader(sys.stdin.buffer) as pcap_reader:  # Use buffer to handle binary data
        for packet in pcap_reader:
            process_packet(packet)

if __name__ == "__main__":
    main()
