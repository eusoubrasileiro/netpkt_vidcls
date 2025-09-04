"""
### For testing:

```bash
sudo tcpdump -i eno1 -s 192 -w - port 80 or port 443 | python3 scapy_sniffer.py --verbose 
```

### For debugging on vscode

#### Start tcpdump in background

```bash
sudo su 
rm /tmp/packet_capture.pcap 
mkfifo /tmp/packet_capture.pcap
tcpdump -i eno1 -w /tmp/packet_capture.pcap 
```

"""

import argparse
import os
import sys
import joblib
import numpy as np 
from datetime import datetime
from scapy.all import PcapReader, IP, TCP, UDP
import pandas as pd
from pathlib import Path
from config import config
from feature_creation import (
    make_windowed_features, 
    load_model,
    preprocess
)
# from blocker import Blocker

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
        #blocker = Blocker() # TODO: uncomment this

    data = []
    start_time = datetime.now()

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

                elapsed = (datetime.now() - start_time).total_seconds()
                if elapsed >= 10:  # Process every 10 seconds
                    if data:  # only process if there is data
                        if not args.train:
                            df_data = preprocess(pd.DataFrame(data))                            
                            if not df_data.empty:
                                # Separate feature columns for prediction
                                feature_cols = config['selected_features']
                                
                                # Iterate over each client found in the time window
                                for client_ip, client_data in df_data.groupby('client'):                                    
                                    features = make_windowed_features(client_data)
                                    client_features = features[feature_cols]
                                    if client_features.empty:
                                        continue # no features to predict
                                    y_proba = model.predict_proba(client_features)
                                    avg_proba = np.mean(y_proba, axis=0)
                                    
                                    is_streaming = avg_proba[1] > config['class-1-threshold']

                                    # Aggregate all server IPs for this client from all their time windows
                                    server_ips = set()
                                    if 'server' in client_data:
                                        server_ips.update(client_data['server'].unique().tolist())
                                    
                                    # Update the blocker with the client's current status
                                    # TODO: only if 3 consecutive classes 1 to 
                                    # also if 3 consecutive classes 0 to remove from streaming state
                                    # that is blocker worker tough... not for here ...
                                    # blocker.update_client_status(client_ip, is_streaming, list(server_ips))

                                    # Log the current activity
                                    status_msg = "IS STREAMING" if is_streaming else "is NOT streaming"
                                    score = 100 * avg_proba[1] if is_streaming else 100 * avg_proba[0]
                                    
                                    # Check if client is currently blocked to reflect in log
                                    client_status = "ALLOWED"
                                    # if client_ip in blocker.clients and blocker.clients[client_ip]['is_blocked']:
                                    #     client_status = "BLOCKED"

                                    print(
                                        f"{datetime.now().strftime('%Y-%m-%d %H:%M:%S')} | "
                                        f"Client: {client_ip:<15} | Status: {client_status:<7} | "
                                        f"Activity: {status_msg:<15} | "
                                        f"Score/Threshold: {score:3.0f}%/{config['class-1-threshold']*100:3.0f}%"
                                    )
                        
                        data.clear()  # Clear data after processing
                    
                    start_time = datetime.now()  # Reset timer
               
    except KeyboardInterrupt: # If the user interrupts the script, save the data
        if args.train:
            df = pd.DataFrame(data)
            df.to_csv(f"training_data_{start_time.isoformat(timespec='minutes')}.csv", index=False)
            print(f"Recorded {len(data)} packets for training.")

    print('No more data to process')


if __name__ == "__main__":
    main()
