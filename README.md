### Real-Time Network Packet Analysis for Video Streaming Identification

**Initial Goal:**

My initial idea was to limit excessive video streaming and social media usage on my home network. I thought I could achieve this directly from my OpenWrt router using C and analyzing network packet traffic (libpcap, tcpdump).

**Revised Approach:**

After some research (mainly discussions with ChatGPT), I realized that packet traffic alone couldn't identify the specific web sources (e.g., Instagram or YouTube). A more comprehensive solution involves combining DNS query logs (dnsmasq) with packet traffic data to train a machine learning classification model (like LSTM or Random Forest).

**Current Focus:**

Since the complete solution is more complex, I'm starting with a simpler goal: identifying video streaming traffic using network packet data and Python for classication using sklearn.

**Key Points:**

* This project aims to analyze network traffic in real-time to identify video streams.
* The initial plan to rely on packet traffic on local linux machine using Python.
* For the future maybe try something that integrates direct to the router, or use a orangepi boar, and incorporate DNS query logs for more accurate source classification.

