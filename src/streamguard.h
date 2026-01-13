/*
 * StreamGuard - Video Streaming Quota Enforcement
 * Header file with context structure and declarations
 */

#ifndef STREAMGUARD_H
#define STREAMGUARD_H

#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <pcap/pcap.h>
#include <ndpi_api.h>
#include "flow_table.h"

/* Configuration constants */
#define MAX_FLOWS 65536
#define SAVE_INTERVAL 60  /* seconds */
#define FLOW_IDLE_TIMEOUT 30  /* seconds */
#define SESSION_TIMEOUT 45000  /* 45 seconds - based on YouTube's ~30s buffer */
#define BLOCK_INITIAL_BACKOFF_MS  5000    /* 5 sec initial retry */
#define BLOCK_MAX_BACKOFF_MS      300000  /* 5 min max backoff */
#define NEXT_BACKOFF(b) ((b) * 2 > BLOCK_MAX_BACKOFF_MS ? BLOCK_MAX_BACKOFF_MS : (b) * 2)
#define LAN_SUBNET "192.168.0.0"
#define LAN_MASK "255.255.255.0"

/* Network constants */
#define ETHERNET_HEADER_SIZE 14
#define MAX_CLIENTS 256  /* Supports /24 subnet */

/* nDPI detection thresholds */
#define NDPI_QUIC_MAX_PACKETS 32   /* More packets needed for QUIC SNI extraction */
#define NDPI_DEFAULT_MAX_PACKETS 10

/* Capture settings */
#define PCAP_SNAPLEN 1600
#define PCAP_TIMEOUT_MS 100
#define BPF_FILTER "port 80 or port 443"

/* Flow tracking structure */
struct flow_info {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t protocol;

    struct ndpi_flow_struct *ndpi_flow;

    ndpi_protocol detected_protocol;
    uint8_t detection_completed;

    uint64_t bytes;
    uint32_t packets;
    uint64_t first_seen_ms;  /* milliseconds */
    uint64_t last_seen_ms;   /* milliseconds */

    uint8_t in_use;
};

/* Client tracking for quota */
struct client_info {
    uint32_t ip;
    uint64_t streaming_seconds;
    char last_reset_date[16];  /* YYYY-MM-DD */
    uint8_t in_use;
    uint8_t is_blocked;
    /* Session-based tracking */
    uint64_t session_start_ms;          /* When current session started (0 = inactive) */
    uint64_t last_streaming_activity_ms; /* Last streaming packet timestamp */
    /* Block retry with exponential backoff */
    uint64_t block_retry_after_ms;      /* 0 = no retry needed, >0 = retry at this time */
    uint32_t block_backoff_ms;          /* Current backoff (doubles each failure) */
};

/* Application context - encapsulates all global state */
struct streamguard_ctx {
    /* nDPI detection module */
    struct ndpi_detection_module_struct *ndpi_module;

    /* Flow and client tracking */
    struct flow_table flow_table;  /* uthash-based flow tracking */
    struct client_info clients[MAX_CLIENTS];

    /* Packet capture */
    pcap_t *pcap_handle;
    volatile int running;

    /* Network configuration */
    uint32_t lan_network;
    uint32_t lan_netmask;

    /* Mode flags */
    int enforce_mode;       /* 0=dry-run, 1=enforce */
    int debug_mode;         /* 0=normal, 1=verbose debug output */
    int video_only_mode;    /* 0=all social media, 1=video streaming only */
    int is_live_capture;    /* 1=live/rpcap, 0=file replay */

    /* Quota settings */
    uint64_t daily_quota;   /* seconds */

    /* State persistence */
    time_t last_state_save;
    char *state_file_path;

    /* Input source */
    char *input_pcap;       /* Read from file/rpcap instead of live capture */
    uint64_t pcap_time_ms;  /* Current time from pcap timestamps (for replay mode) */

    /* Remote router configuration */
    char *router_ip;        /* Router IP for SSH commands */
    char *ssh_user;         /* SSH user (default: root) */
    char *ssh_keyfile;      /* SSH key path (optional) */
    char *firewall_type;    /* "nft" (nftables) or "ipset" (iptables) */

    /* Logging */
    char *log_file_path;
    FILE *log_file_fp;
};

/* Context initialization and cleanup */
void streamguard_ctx_init(struct streamguard_ctx *ctx);
void streamguard_ctx_free(struct streamguard_ctx *ctx);

/*
 * Core functions exposed for testing via STATIC macro.
 * When compiled with -DTESTING, these are non-static.
 * Current signatures (using global state via macros):
 */
#ifdef TESTING
int is_social_media_protocol(ndpi_protocol proto);
int is_trackable_traffic(ndpi_protocol proto);
int is_lan_ip(uint32_t ip);
int get_client_index(uint32_t ip);
void normalize_flow_key(uint32_t *ip1, uint32_t *ip2, uint16_t *port1, uint16_t *port2);
uint32_t flow_hash(uint32_t src_ip, uint32_t dst_ip, uint16_t src_port,
                   uint16_t dst_port, uint8_t protocol);
void save_state(void);
void load_state(void);

/* Test helpers */
void test_set_lan_network(uint32_t network, uint32_t netmask);
void test_set_video_only_mode(int mode);
void test_set_state_file_path(const char *path);
void test_init_client(int idx, uint32_t ip, uint64_t seconds, const char *date);
void test_get_client(int idx, uint32_t *ip, uint64_t *seconds, int *blocked);
int test_client_in_use(int idx);
void test_clear_clients(void);
#endif

#endif /* STREAMGUARD_H */
