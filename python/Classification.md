### Indentifiying/Classifing video streaming traffic features for classification.

First identify clients on my LAN (local network).
Using subnet 192.168.0.0/24 - 32 - 24 = 8 bits - 256 clients is simple.
Ignoring broadcast address (router) 192.168.0.1.

### Within a 10 seconds window calculate:

1. Is there packets?
2. Identify all clients that had traffic of packets during this period
3. For each client register the server ips
4. With the packet data create features and windowed features:
   - Some the features are bellow look `feature_creation.py` for more details    
      - calculate average upload speed for specific client
      - calculate average download speed for specific client
      - packet size variance: get all packets sizes received and calculate the variance
      - packet size average : get all packets sizes and make an average
      - packet average delay time: delay between received packets - packet arrival time difference make an average
      - packet variance delay time: the variance of the previous mesurament
      - *Connection Multiplexing*: many video streaming open multiple connections to different servers or CDNS's simultaneously.
      - for every packet get source ip and destination ip, store all the ips involved and calculate the nunique ips.
      - Group the top n (10) and get each one of them the traffic average volume.


### Training and Classification

For creating training data use a specific local machine, no mix of clients (single client).
For classification use the ssh+tcpdump to OpenWrt router to br-lan interface and many clients can be classified at once at every 10 seconds.

### Lessons Learned

Model so far with ExtraTrees (default parameters) is getting aroung 93% mean accuracy with 10 seconds window.
Using smaller windows decrease accuracy. Removing entropy features also decreases accuracy.
Already selected the best first 11 features.

#### Other Trafic Metrics

- Bytes/10s (throughput) ← best default
- Packets/10s (pps) (useful to spot DNS floods/scans)
- New flows/10s (count new 5-tuples; good for scanners)
- Unique destinations/10s (fan-out; scanning vs streaming)
- Optional: avg packet size, % UDP/QUIC, % retransmits
