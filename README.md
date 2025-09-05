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

Current working version was trained:
     - with 184.9MB of training data 
     - 8 local manually classified scenarios 

We can run it with bellow specifiying the interface `-i eno1` by its name

```bash
sudo tcpdump -i eno1 -s 1024 -w - port 80 or port 443 | python3 scapy_sniffer.py --verbose 
```

##### Real Time Classification

Use 3 consecutive classes 1 classifications as a trigger. (30 seconds window)
Also use class 1 threshold of 70% to classify as real streaming. 
For registering an ip in a state of watching a the video streaming. 
And to remove it from the video streaming state another 3 classes 0. 

### **But that's isn't enough for real time classification

> Buffering and ABR (adaptive bitrate) make traffic bursty (2–6 s segment downloads, then silence). 
If you naïvely count “active time” only when bps > threshold, you’ll under-count during the silent gaps,

GPT5 suggested:

> 1. **Sliding window + EWMA.** Compute bytes/sec every `W=2–5s`, but smooth with an EWMA so spikes don’t flip states.
> 2. **Hysteresis thresholds.**
>   * Enter PLAYING if `rate_ewma ≥ START_T` for **K** consecutive windows (e.g., K=2).
>   * Stay PLAYING until `rate_ewma < STOP_T` for **M** consecutive windows (e.g., M=6).
>   * Use `STOP_T` < `START_T` (e.g., 150 kbps vs 400 kbps).
> 3. **Buffer-credit accounting.** When in PLAYING and you see a big burst, add
>   ```
>   buffer_credit_seconds += bytes_in_window / est_bitrate
>   ```
>   Clamp `buffer_credit_seconds` to, say, **90 s** per (client, provider). During quiet gaps, **decrement** credit and **keep counting time** as PLAYING until it hits zero. This matches how players fetch a few segments, then coast.
>
>   * Keep a rolling `est_bitrate` per (client, provider) = EWMA of observed rate while in PLAYING.
> 4. **Minimum session length.** Ignore “sessions” that last <10 s to avoid counting previews/autoplay thumbnails.
> 5. **Cooldown before blocking.** When quota is hit, add `(client . server)` to your nftables set with a short **grace (e.g., 30 s)** first or show a message; then extend to your full block timeout. This prevents instant oscillation if the app  retries on a different CDN IP.
> 6. **Aggregate by provider, not single IP.** Track state per `(client, provider)` (YouTube/Netflix/etc.). Multiple CDN IPs rotate under one session; your ip-pair blocklist is only for enforcement.


#### Running from orangepi5


```bash
# adjust OPENWRT_IP, key, interface as needed
OPENWRT_IP=192.168.0.1

ssh -o ServerAliveInterval=30 root@$OPENWRT_IP \
  "tcpdump -i br-lan -s 192 -nn -w  - 'port 80 or port 443'" \
| python3 scapy_sniffer.py --verbose
```

#### TODO

Rethink approach... like sessions or use `tshark` (open source wire-shark)
Maybe analyze pair of ip's and training is completly wrong. 
Should make another aproach for feature creation and training.

1. Create blocker.py to manage a local JSON file with client states. 
If 3 consecutive flags for one client put it on streaming state and start to count its time.
If it reaches the quota limit specified in `config['streaming-limit-seconds']` start writting the server ips on a txt. 
One per time, at every new call to blocker add another one, starting from the ones with highest traffic to the ones to the lowest traffic. 
That gives time for the client to move to another state and lettings us block only video streaming server ips. 
2. Create a tiny static python? server to serve real time logs and client states and time quota.

##### Future Ideas

###### Classification 

- If we use a threshold on the predicted probabilities for class 1 the desired event. 
We can reach even 99% of avg. precision... how much I care for recall?


~~- Try incorporate DNS query logs for more accurate source classification.~~

