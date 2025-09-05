### Real-Time Network Packet Analysis for Video Streaming Identification and Blocking

This project ~~identifies clients on a local network that are watching video streaming~~. (**!!Only identifies some bursts**)
After a configurable daily time quota (default 30 min), blocks the destination servers they are connected to. (**Not done yet**)
This is achieved by analyzing network traffic in real-time and dynamically updating `nftables` blacklisting servers ips. (**Not done yet**)
For those it relies on a OpenWrt router with `tcpdump` and `nftables` + a SBC (orangepi5 etc or another) with python support. (**Not done yet**)


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



### GPT5 ANSWER

Short answer: yes—buffering and ABR (adaptive bitrate) make traffic **bursty** (2–6 s segment downloads, then silence). If you naïvely count “active time” only when bps > threshold, you’ll under-count during the silent gaps, and if you only count bytes you can over-count big prefetches. Fix it with a small state machine + smoothing.

Here’s a battle-tested approach that works well in home networks:

## How to make it robust (no flapping)

1. **Watch UDP/443 too.** Most video is QUIC.
2. **Sliding window + EWMA.** Compute bytes/sec every `W=2–5s`, but smooth with an EWMA so spikes don’t flip states.
3. **Hysteresis thresholds.**

   * Enter PLAYING if `rate_ewma ≥ START_T` for **K** consecutive windows (e.g., K=2).
   * Stay PLAYING until `rate_ewma < STOP_T` for **M** consecutive windows (e.g., M=6).
   * Use `STOP_T` < `START_T` (e.g., 150 kbps vs 400 kbps).
4. **Buffer-credit accounting.** When in PLAYING and you see a big burst, add

   ```
   buffer_credit_seconds += bytes_in_window / est_bitrate
   ```

   Clamp `buffer_credit_seconds` to, say, **90 s** per (client, provider). During quiet gaps, **decrement** credit and **keep counting time** as PLAYING until it hits zero. This matches how players fetch a few segments, then coast.

   * Keep a rolling `est_bitrate` per (client, provider) = EWMA of observed rate while in PLAYING.
5. **Minimum session length.** Ignore “sessions” that last <10 s to avoid counting previews/autoplay thumbnails.
6. **Cooldown before blocking.** When quota is hit, add `(client . server)` to your nftables set with a short **grace (e.g., 30 s)** first or show a message; then extend to your full block timeout. This prevents instant oscillation if the app retries on a different CDN IP.
7. **Aggregate by provider, not single IP.** Track state per `(client, provider)` (YouTube/Netflix/etc.). Multiple CDN IPs rotate under one session; your ip-pair blocklist is only for enforcement.

## Tiny drop-in logic (adapt to your script)

```python
# --- tuning ---
WINDOW = 3.0                        # seconds
START_T = 400_000                   # ~400 kbps start
STOP_T  = 150_000                   # ~150 kbps stop
K_CONSEC_START = 2
M_CONSEC_STOP  = 6
MAX_BUFFER_SEC = 90
ALPHA = 0.3                         # EWMA weight

# per (client, provider) state
state = {}  # key -> dict(play=False, ewma=0, est_bitrate=800_000, k=0, m=0, buf=0, used=0)

def tick(client_ip, provider, bytes_in_window):
    key = (client_ip, provider)
    s = state.setdefault(key, dict(play=False, ewma=0, est_bitrate=800_000,
                                   k=0, m=0, buf=0.0, used=0))
    rate = bytes_in_window / WINDOW  # bytes/sec
    s['ewma'] = (1-ALPHA)*s['ewma'] + ALPHA*rate

    # update est_bitrate only when playing (stabilizes to what this device really uses)
    if s['play']:
        s['est_bitrate'] = 0.8*s['est_bitrate'] + 0.2*max(s['ewma'], 100_000)

    # accumulate buffer on bursts while playing (or when just starting)
    if s['ewma'] >= START_T:
        s['buf'] = min(MAX_BUFFER_SEC, s['buf'] + bytes_in_window / max(s['est_bitrate'], 1))

    if not s['play']:
        s['k'] = s['k'] + 1 if s['ewma'] >= START_T else 0
        if s['k'] >= K_CONSEC_START:
            s['play'] = True
            s['k'] = 0
            # seed a little buffer at start
            s['buf'] = min(MAX_BUFFER_SEC, s['buf'] + 5)
    else:
        if s['ewma'] < STOP_T:
            s['m'] += 1
        else:
            s['m'] = 0
        # consume buffer during quiet gaps; count time as playing while buf > 0
        playing_this_window = True
        if s['ewma'] < STOP_T:
            s['buf'] = max(0.0, s['buf'] - WINDOW)
            if s['buf'] <= 0 and s['m'] >= M_CONSEC_STOP:
                s['play'] = False
                playing_this_window = False
                s['m'] = 0

        if playing_this_window:
            s['used'] += WINDOW  # this is your “watch-time” to compare vs quota

    return s['play'], s['used']       # you can act on quota elsewhere
```

* Feed `tick()` once per WINDOW with the **summed bytes** you measured for that `(client, provider)` in the last window.
* When `used ≥ quota_seconds[client][provider]`, push the `(client . server_ip)` into your nftables set (as you already do).
* Keep a short **hold timer** (e.g., 20–30 s) before inserting the block to avoid punishing momentary spikes.

## Why this handles buffering well

* **Segment cadence** is absorbed by `MAX_BUFFER_SEC` and the EWMA/hysteresis. You won’t flip to IDLE during the silent gaps between segments; you keep counting down the buffer.
* **Prefetch spikes** don’t gift hours of watch-time: the credit is capped, and later idle windows “spend” the credit at real-time speed.
* **Quality changes (ABR)** auto-adjust via `est_bitrate`—your bytes→seconds conversion tracks what the device is actually pulling.

## Practical defaults to start

* `WINDOW=3s`, `START_T=400 kbps`, `STOP_T=150 kbps`, `MAX_BUFFER_SEC=90s`
* Start with per-device daily quotas like 30–60 min for “Video” bucket (sum of YouTube + Netflix + Reels/TikTok).
* Log decisions for a day before enforcing; check a few manual sessions to calibrate thresholds.

If you want, I can splice this into the earlier skeleton (with SNI/DNS → provider mapping, nftables insert) so you can run it right away on the Orange Pi 5.

