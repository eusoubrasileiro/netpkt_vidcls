### Real-Time Network Packet Analysis for Video Streaming Identification and Blocking

This project identifies clients on a local network that are watching video streaming. 
After a configurable daily time quota (default 30 min), blocks the destination servers they are connected to. 
This is achieved by analyzing network traffic in real-time and dynamically updating `nftables` blacklisting servers ips.
For those it relies on a OpenWrt router with `tcpdump` and `nftables` + a SBC (orangepi5 etc or another) with python support.


#### How It Works

1.  **Packet Capture:** `tcpdump` captures network traffic from a specified interface (e.g., your LAN interface).
2.  **Real-Time Analysis:** The captured traffic is piped to `scapy_sniffer.py`. This script analyzes the packets in 10-second windows.
3.  **Per-Client Classification:** For each client on the local network, the script uses a pre-trained Extra Trees machine learning model (`etree.joblib`) to classify their traffic as "video streaming" or "not video streaming".
4.  **Time Tracking:** The `blocker.py` module tracks the cumulative streaming time for each client over a 24-hour period.
5.  **Automated Blocking:** If a client's total streaming time exceeds the configured limit (e.g., 30 min.), the `blocker` module identifies the server IPs they are streaming from and adds them to a `nftables` blocklist file (e.g. `/etc/blocked-ips-v4.txt`) configured by a nft file using `uci firewall` openwrt.
6.  **DNS Blackholing:** `dnsmasq` is automatically signaled to reload its configuration, effectively blocking the client's access to the streaming service by redirecting its DNS queries to `0.0.0.0`.
7.  **State Persistence:** The streaming duration for each client is saved in `streaming_state.json`, so the script can be restarted without losing track of usage.

---

#### Setup and Configuration

1.  **Dependencies:** Ensure you have Python 3, `tcpdump`, and `dnsmasq` installed. You will also need the required Python libraries:
    ```bash
    pip install pandas scapy joblib numpy
    ```

#### Current Status

So far we have the classification working by sampling network traffic every 10 seconds by 10 seconds. 
An ExtraTree model using features create from header-packets (10 seconds of traffic data) are able to classify the trafic as video streaming or not.
Average 90% of binary average classification accuracy on random splitting scenarios with tranining data.
Validated the 90% on some real scenarios using some good estimations (50% threshold).

Current working version was trained:
     - with 184.9MB of training data 
     - 8 local manually classified scenarios 

We can run it with bellow specifiying the interface `-i eno1` by its name

```bash
sudo tcpdump -i eno1 -s 1024 -w - port 80 or port 443 | python3 scapy_sniffer.py --verbose 
```

##### Real Time Classification

Use 3 consecutive classes 1 classifications as a trigger. (30 seconds window)
For registering an ip in a state of watching a the video streaming. 
And to remove it from the video streaming state another 3 classes 0. 

#### Running from orangepi5


```bash
# adjust OPENWRT_IP, key, interface as needed
OPENWRT_IP=192.168.0.1

ssh -o ServerAliveInterval=30 root@$OPENWRT_IP \
  "tcpdump -i br-lan -s 192 -nn -w  - 'port 80 or port 443'" \
| python3 scapy_sniffer.py --verbose
```



##### Future Ideas

###### Classification 

- If we use a threshold on the predicted probabilities for class 1 the desired event. 
We can reach even 99% of avg. precision... how much I care for recall?


~~- Try incorporate DNS query logs for more accurate source classification.~~

