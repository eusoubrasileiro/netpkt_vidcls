### Real-Time Network Packet Analysis for Video Streaming Identification

The goal is to limit excessive video streaming and social media usage on my home network.  
We could theoretically achieve this by analyzing network packet traffic (libpcap, tcpdump).  
Initially tought about using C and running directly on my OpenWrt router. 
But for start it's easier to somehow redirect the traffic to a SBC (orangepi5 etc or another).
There classify the source ip on our network and if it's a streaming traffic and for how long. 
If streaming and for too long start blacklisting the external-destination ips on dnsmasq.
Eventually reset every night those ips on dnsmasq.


#### Current Status

So far we have the classification working by sampling network traffic every 10 seconds. 
An ExtraTree model and some features create from header-packets (10 seconds of traffic data) are able to classify the trafic as video streaming or not.
Average 90% of binary average classification accuracy on random splitting scenarios with tranining data.
Validated the 90% on some real scenarios 50% threshold using some good estimations.

Current working version was trained:
     - with 184.9MB of training data 
     - 8 local manually classified scenarios 

We can run it with bellow specifiying the interface `-i eno1` by its name

```bash
sudo tcpdump -i eno1 -s 1024 -w - port 80 or port 443 | python3 scapy_sniffer.py --verbose 
```

#### Real Time Plan

This can already work using 3 consecutive classes 1 classifications as a trigger. (for now 30 seconds)
For registering an ip in a state of watching a the video streaming. 
And to remove it from the video streaming state another 3 classes 0. 

##### Future Ideas

###### Classification 

- If we use a threshold on the predicted probabilities for class 1 the desired event. 
We can reach even 99% of avg. precision... how much I care for recall?


~~- Try incorporate DNS query logs for more accurate source classification.~~

