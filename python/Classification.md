### Indentifiying/Classifing video streaming traffic features for classification.

Get all ips within lan and ...
First of ALL: Identify the client the packets are talking about!
 
### Within a 5 seconds window calculate:

The problem is that during 5 seconds we might get packets from different clients. 
Problem solved above. 

 - calculate average upload speed for specific client?
 - calculate average upload speed for specific client?
 - packet size variance: get all packets sizes received and calculate the variance
 - packet size average : get all packets sizes and make an average
 - packet average delay time: delay between received packets - get all packets time differences between then and make an average
 - packet variance delay time: the variance of the previous mesurament
 Connection Multiplexing - many video streaming open multiple connections to different servers or CDNS's simultaneously. 
 - for every packet get source ip and destination ip, store all the ips involved and calculate the nunique ips. 
 - Group the top n (10) and get their traffic average volume. ?

