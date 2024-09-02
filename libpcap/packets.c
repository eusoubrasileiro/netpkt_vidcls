// program that intercepts network packets (can be rtsp, udp, tcp, http, dns traffic or whatever else...)
// no port is specified so getting everything
// usage sudo ./packets wlp2s0 - single client packet analyses wifi interface 09
// prints current traffic as packets per seconds (mixing up or download)

#include <pcap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <net/ethernet.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>

#define SNAP_LEN 1518  // Maximum bytes to capture per packet - since most are encripted their data useless
#define TIMEOUT_MS 1000 // Capture timeout in milliseconds

void process_packet(u_char *args, const struct pcap_pkthdr *header, const u_char *packet);

time_t previous_time;

int main(int argc, char *argv[]) {
    char *dev = argv[1];  // Network device to capture from
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_t *handle;

    // initialize 
    previous_time =  time(NULL); // current time is the previous time     
    if (dev == NULL) {
        fprintf(stderr, "No device specified. Exiting.\n");
        return 1;
    }
    printf("Opening device %s\n", dev);
    // Open the session in promiscuous mode
    handle = pcap_open_live(dev, SNAP_LEN, 1, TIMEOUT_MS, errbuf);
    if (handle == NULL) {
        fprintf(stderr, "Couldn't open device %s: %s\n", dev, errbuf);
        return 2;
    }
    // Capture packets in a loop
    pcap_loop(handle, 0, process_packet, NULL);

    // Close the session
    pcap_close(handle);
    return 0;
}

// For HD it's around 5Mb/s or 8Mb/s - Between 4000 to 6000 packets per second
// Packet handler function
void process_packet(u_char *args, const struct pcap_pkthdr *header, const u_char *packet) {
    char buffer[50];
    static int packet_count = 0;    
    struct iphdr *ip_header = (struct iphdr *)(packet + sizeof(struct ethhdr)); // IP header offset
    // Get current time  
    time_t current_time = time(NULL);    
    double elapsed_time = difftime(current_time, previous_time);
    previous_time = current_time; 
    // Update packet count
    packet_count++;
    if (elapsed_time >= 0.5){ // only print every half a second
        //printf("\r packets/s: %f", packet_count / elapsed_time);    
        printf("packets/s: %f\n", packet_count / elapsed_time);            
        fflush(stdout); // Still essential!
    }
    // get a sample packet at every 500 packets
    if(packet_count%500!=0)// get 4 to 10 packets per second on HD video - not much    
        return;
    packet_count = 0; // restart count 
    // printf("Raw packet data:\n");
    // print_raw_data(packet, header->caplen);
    // printf("IP version: %d\n", ip_header->version);
    // printf("Packet captured: Ethernet type: 0x%04x\n", ntohs(eth->h_proto));    

    struct in_addr src_ip, dst_ip;
    src_ip.s_addr = ip_header->saddr;
    dst_ip.s_addr = ip_header->daddr;

    char src_ip_str[INET_ADDRSTRLEN];
    char dst_ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &src_ip, src_ip_str, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &dst_ip, dst_ip_str, INET_ADDRSTRLEN);

    printf("Packet to %s detected from %s\n", dst_ip_str, src_ip_str);
    // Check if the destination IP matches any monitored IPs
    // for (int i = 0; i < sizeof(monitored_ips) / sizeof(monitored_ips[0]); i++) {
    //     if (strcmp(dst_ip_str, monitored_ips[i]) == 0) {
    //         printf("Packet to %s detected from %s at %s", monitored_ips[i], src_ip_str, ctime(&(header->ts.tv_sec)));
    //         // You can write this information to a log file or perform other actions
    //     }        
    // }
    // Print packet information or perform further analysis
}




