To create training samples data files

```bash
sudo tcpdump -i wlp2s0 -s 1024 -w - port 80 or port 443 | python3 scapy_sniffer.py --train > youtube_packets_01.txt
```

#### List of training Data

Files parsed already ingested by raw.h5 pandas dataframe:

- working_packets_01.txt web activities: spotify piano playing, teams, whatsapp
- working_packets_02.txt web activities: unimed, teams anp, outlook, drive, sei, google search ...
- working_packets_03.txt web activities: nothing: locked screen
- youtube_packets_01.txt web activities: youtube real crusades history 11 minutes
- youtube_packets_02.txt web activities: youtube veritasium 20 minutes
- youtube_packets_03.txt web activities: youtube shorts for some minutes > 10 minutes
- insta_packets_01.txt web activities: instagram scrolling and videos
- insta_packets_02.txt web activities: instagram scrolling and videos



#### Needed 


- Audio streaming (audible, spotify, pilgrim) only to classify as not video streaming