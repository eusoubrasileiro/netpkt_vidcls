"""
sudo su 
rm /tmp/packet_capture.pcap 
mkfifo /tmp/packet_capture.pcap
tcpdump -i eno1 -w /tmp/packet_capture.pcap 
"""

import argparse
import os
import sys
import joblib
import numpy as np 
import datetime
from scapy.all import PcapReader, IP, TCP, UDP
import pandas as pd
from pathlib import Path
from feature_creation import (
    make_windowed_features, 
    config,
    load_model,
    preprocess
)

def process_packet(packet):
    """Process individual packet to extract relevant fields."""
    if not packet.haslayer(IP):
        return None
    ip_layer = packet[IP]
    packet_data = {
        'src_ip': ip_layer.src,
        'dst_ip': ip_layer.dst,
        'packet_size': ip_layer.len,
        'time': float(packet.time),
        'identification': ip_layer.id,
        'ttl': ip_layer.ttl,
        'ip_flags': str(ip_layer.flags) if ip_layer.flags else "NONE",
        'tcp_sport': -1, 'tcp_dport': -1, 'tcp_seq': -1, 'tcp_ack': -1, 'tcp_flags': "NONE",
        'udp_sport': -1, 'udp_dport': -1,
    }
    if packet.haslayer(TCP):
        tcp_layer = packet[TCP]
        packet_data.update({
            'tcp_sport': tcp_layer.sport,
            'tcp_dport': tcp_layer.dport,
            'tcp_seq': tcp_layer.seq,
            'tcp_ack': tcp_layer.ack,
            'tcp_flags': str(tcp_layer.flags) if tcp_layer.flags else "NONE",
        })
    if packet.haslayer(UDP):
        udp_layer = packet[UDP]
        packet_data.update({
            'udp_sport': udp_layer.sport,
            'udp_dport': udp_layer.dport,
        })
    return packet_data

def get_pcap_reader(source):
    """Get PcapReader for the specified source."""
    if source == "stdin":
        return PcapReader(sys.stdin.buffer)
    elif os.path.exists(source):
        return PcapReader(source)
    else:
        raise FileNotFoundError(f"Specified source '{source}' does not exist.")

def main():
    parser = argparse.ArgumentParser(description="Network traffic sniffer and feature extractor.")
    parser.add_argument("--train", action="store_true", default=False, help="Record data for model training.")    
    parser.add_argument("--verbose", action="store_true", help="Print packet details during training.")
    parser.add_argument("--source", type=str, default="stdin", help="Input source: 'stdin' or path to named pipe (FIFO).")
    args = parser.parse_args()

    # Verify source if not stdin
    if args.source != "stdin" and not os.path.exists(args.source):
        raise FileNotFoundError(f"The specified source '{args.source}' does not exist.")
    
    if not args.train:        
        print("Starting inference...")
        model = load_model()

    data = []
    start_time = datetime.datetime.now()

    try:
        with get_pcap_reader(args.source) as pcap_reader:
            for packet in pcap_reader:
                packet_data = process_packet(packet)
                if packet_data:
                    data.append(packet_data)
                    if args.verbose and args.train:
                        print((
                            f"Packet: src {packet_data['src_ip']:>15} -> dst {packet_data['dst_ip']:>15} "
                            f"size {packet_data['packet_size']:6d} tcp_sport {packet_data['tcp_sport']:6d} "
                            f"tcp_dport {packet_data['tcp_dport']:6d} udp_sport {packet_data['udp_sport']:6d} "
                            f"udp_dport {packet_data['udp_dport']:6d} time {packet_data['time']:18.6f} "
                            f"ID:{packet_data['identification']:5d} TTL:{packet_data['ttl']:3d} "
                            f"IP Flags:{packet_data['ip_flags']} TCP Seq:{packet_data['tcp_seq']:10d} "
                            f"TCP Ack:{packet_data['tcp_ack']:10d} TCP Flags:{packet_data['tcp_flags']}"
                        ))

                elapsed = (datetime.datetime.now() - start_time).total_seconds()
                if elapsed >= 10:  # Process every 10 seconds                
                    if not args.train:
                        X = make_windowed_features(preprocess(pd.DataFrame(data)))
                        X = X[config['selected_features']]                        
                        y = model.predict_proba(X)
                        n = y.shape[0]
                        y = np.sum(y, axis=0)/n  # average the predictions (in case more then 1 prediction is made)
                        # cuttoff at 50% 
                        if y[1] > 0.5:
                            print(f"{datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')} You ARE WATCHING A VIDEO streaming. Score: {100*y[1]:3.0f}%")
                        else:
                            print(f"{datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')} You ARE NOT watching A VIDEO streaming. Score: {100*y[0]:3.0f}%")
                        data.clear()
                    start_time = datetime.datetime.now()
               
    except KeyboardInterrupt: # If the user interrupts the script, save the data
        if args.train:
            df = pd.DataFrame(data)
            df.to_csv(f"training_data_{start_time.isoformat(timespec='minutes')}.csv", index=False)
            print(f"Recorded {len(data)} packets for training.")

    print('No more data to process')


if __name__ == "__main__":
    main()
