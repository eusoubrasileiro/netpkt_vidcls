### Real-Time Network Packet Analysis for Video Streaming Identification

The goal is to limit excessive video streaming and social media usage on my home network.  
We could theoretically achieve this by analyzing network packet traffic (libpcap, tcpdump).  
Initially tought about using C and running directly on my OpenWrt router. 
But for start it's easier to somehow redirect the traffic to a SBC (orangepi5 etc or another).
There classify the source ip on our network and if it's a streaming traffic and for how long. 
If streaming and for too long start blacklisting the external-destination ips on dnsmasq.
Eventually reset every night those ips on dnsmasq.


#### Current Status

So far we have the classification working.
An ExtraTree model and some features are able to classify the trafic as video streaming or not.
With 90% of average accuracy on random splitting scenarios with tranining data.

Current working version was trained:
     - with 184.9MB of training data 
     - 8 locally manual classified scenarios 

We can run it with bellow specifiying the interface `-i eno1` is the name bellow

```bash
sudo tcpdump -i eno1 -s 1024 -w - port 80 or port 443 | python3 scapy_sniffer.py --verbose 
```

##### Future ideas

###### Classification 

-  Try incorporate DNS query logs for more accurate source classification.

