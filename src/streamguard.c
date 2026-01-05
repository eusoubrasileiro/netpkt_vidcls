/*
 * StreamGuard - Video Streaming Quota Enforcement
 *
 * Uses nDPI for protocol detection (YouTube, Netflix, TikTok, etc.)
 * and tracks watch time per client for quota enforcement.
 *
 * Compile:
 *   gcc -o streamguard streamguard.c $(pkg-config --cflags --libs libndpi) -lpcap
 *
 * Run:
 *   sudo ./streamguard -i eth0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <pcap.h>
#include <ndpi/ndpi_api.h>
#include <cjson/cJSON.h>
#include <sys/stat.h>
#include <errno.h>

#define MAX_FLOWS 65536
#define STATE_FILE "streamguard_state.json"
#define SAVE_INTERVAL 60  /* seconds */
#define FLOW_IDLE_TIMEOUT 30  /* seconds */
#define LAN_SUBNET "192.168.0.0"
#define LAN_MASK "255.255.255.0"

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
};

/* Get current time in milliseconds */
static uint64_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Globals */
static struct ndpi_detection_module_struct *ndpi_module = NULL;
static struct flow_info flows[MAX_FLOWS];
static struct client_info clients[256];  /* /24 subnet */
static pcap_t *pcap_handle = NULL;
static volatile int running = 1;
static uint32_t lan_network;
static uint32_t lan_netmask;
static int enforce_mode = 0;  /* 0=dry-run, 1=enforce */
static uint64_t daily_quota = 3600;  /* seconds */
static time_t last_state_save = 0;
static char *state_file_path = NULL;

/* Streaming protocol IDs we care about */
static int is_streaming_protocol(ndpi_protocol proto) {
    uint16_t app = proto.app_protocol;
    uint16_t master = proto.master_protocol;

    /* Check both master and app protocol */
    uint16_t p = (app != NDPI_PROTOCOL_UNKNOWN) ? app : master;

    switch (p) {
        /* Video Streaming */
        case NDPI_PROTOCOL_YOUTUBE:
        case NDPI_PROTOCOL_NETFLIX:
        case NDPI_PROTOCOL_TIKTOK:
        case NDPI_PROTOCOL_TWITCH:
        case NDPI_PROTOCOL_DISNEYPLUS:
        case NDPI_PROTOCOL_AMAZON_VIDEO:
        case NDPI_PROTOCOL_HULU:
        /* Social Media (often has video) */
        case NDPI_PROTOCOL_INSTAGRAM:
        case NDPI_PROTOCOL_FACEBOOK:
        case NDPI_PROTOCOL_SNAPCHAT:
        case NDPI_PROTOCOL_WHATSAPP:
            return 1;
        default:
            return 0;
    }
}

/* Check if IP is in LAN */
static int is_lan_ip(uint32_t ip) {
    return (ip & lan_netmask) == lan_network;
}

/* Get client index from IP (assumes /24) */
static int get_client_index(uint32_t ip) {
    return ip & 0xFF;
}

/* Get current date as YYYY-MM-DD string */
static void get_current_date(char *buf, size_t len) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    strftime(buf, len, "%Y-%m-%d", tm);
}

/* Block client via nftables */
static void block_client(uint32_t client_ip) {
    char ip_str[INET_ADDRSTRLEN];
    struct in_addr addr = {.s_addr = client_ip};
    inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str));

    int idx = get_client_index(client_ip);
    if (clients[idx].is_blocked) return;  /* Already blocked */

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
        "nft add element inet fw4 blocked_streaming_clients '{ %s timeout 24h }'",
        ip_str);

    if (enforce_mode) {
        int ret = system(cmd);
        if (ret == 0) {
            clients[idx].is_blocked = 1;
            printf("[BLOCKED] %s (quota exceeded: %lu/%lu seconds)\n",
                   ip_str, clients[idx].streaming_seconds, daily_quota);
        } else {
            printf("[ERROR] Failed to block %s (nft returned %d)\n", ip_str, ret);
        }
    } else {
        printf("[DRY-RUN] Would block %s (quota exceeded: %lu/%lu seconds)\n",
               ip_str, clients[idx].streaming_seconds, daily_quota);
    }
}

/* Unblock client via nftables */
static void unblock_client(uint32_t client_ip) {
    char ip_str[INET_ADDRSTRLEN];
    struct in_addr addr = {.s_addr = client_ip};
    inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str));

    int idx = get_client_index(client_ip);
    if (!clients[idx].is_blocked) return;  /* Not blocked */

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
        "nft delete element inet fw4 blocked_streaming_clients '{ %s }'",
        ip_str);

    if (enforce_mode) {
        int ret = system(cmd);
        if (ret == 0) {
            clients[idx].is_blocked = 0;
            printf("[UNBLOCKED] %s (daily reset)\n", ip_str);
        }
    } else {
        printf("[DRY-RUN] Would unblock %s (daily reset)\n", ip_str);
    }
}

/* Save state to JSON file */
static void save_state(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON *clients_arr = cJSON_CreateArray();

    for (int i = 0; i < 256; i++) {
        if (!clients[i].in_use) continue;

        char ip_str[INET_ADDRSTRLEN];
        struct in_addr addr = {.s_addr = clients[i].ip};
        inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str));

        cJSON *client = cJSON_CreateObject();
        cJSON_AddStringToObject(client, "ip", ip_str);
        cJSON_AddNumberToObject(client, "streaming_seconds", clients[i].streaming_seconds);
        cJSON_AddStringToObject(client, "last_reset_date", clients[i].last_reset_date);
        cJSON_AddBoolToObject(client, "is_blocked", clients[i].is_blocked);
        cJSON_AddItemToArray(clients_arr, client);
    }

    cJSON_AddItemToObject(root, "clients", clients_arr);

    char *json_str = cJSON_Print(root);
    const char *path = state_file_path ? state_file_path : STATE_FILE;
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(json_str, f);
        fclose(f);
    } else {
        fprintf(stderr, "[WARN] Could not save state to %s: %s\n", path, strerror(errno));
    }

    free(json_str);
    cJSON_Delete(root);
    last_state_save = time(NULL);
}

/* Load state from JSON file */
static void load_state(void) {
    const char *path = state_file_path ? state_file_path : STATE_FILE;
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("No previous state file found (%s), starting fresh\n", path);
        return;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *json_str = malloc(fsize + 1);
    if (!json_str) {
        fclose(f);
        return;
    }
    size_t bytes_read = fread(json_str, 1, fsize, f);
    json_str[bytes_read] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);

    if (!root) {
        fprintf(stderr, "[WARN] Could not parse state file\n");
        return;
    }

    cJSON *clients_arr = cJSON_GetObjectItem(root, "clients");
    if (!cJSON_IsArray(clients_arr)) {
        cJSON_Delete(root);
        return;
    }

    int loaded = 0;
    cJSON *client;
    cJSON_ArrayForEach(client, clients_arr) {
        cJSON *ip_json = cJSON_GetObjectItem(client, "ip");
        cJSON *seconds_json = cJSON_GetObjectItem(client, "streaming_seconds");
        cJSON *date_json = cJSON_GetObjectItem(client, "last_reset_date");
        cJSON *blocked_json = cJSON_GetObjectItem(client, "is_blocked");

        if (!cJSON_IsString(ip_json) || !cJSON_IsNumber(seconds_json)) continue;

        struct in_addr addr;
        if (inet_pton(AF_INET, ip_json->valuestring, &addr) != 1) continue;

        int idx = get_client_index(addr.s_addr);
        clients[idx].ip = addr.s_addr;
        clients[idx].streaming_seconds = (uint64_t)seconds_json->valuedouble;
        clients[idx].in_use = 1;

        if (cJSON_IsString(date_json)) {
            snprintf(clients[idx].last_reset_date,
                     sizeof(clients[idx].last_reset_date), "%s", date_json->valuestring);
        }
        if (cJSON_IsBool(blocked_json)) {
            clients[idx].is_blocked = cJSON_IsTrue(blocked_json);
        }
        loaded++;
    }

    cJSON_Delete(root);
    printf("Loaded state for %d clients from %s\n", loaded, path);
}

/* Check if daily reset is needed */
static void check_daily_reset(void) {
    char today[16];
    get_current_date(today, sizeof(today));

    for (int i = 0; i < 256; i++) {
        if (!clients[i].in_use) continue;

        /* If last_reset_date is empty or different from today, reset */
        if (clients[i].last_reset_date[0] == '\0' ||
            strcmp(clients[i].last_reset_date, today) != 0) {

            char ip_str[INET_ADDRSTRLEN];
            struct in_addr addr = {.s_addr = clients[i].ip};
            inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str));

            if (clients[i].streaming_seconds > 0) {
                printf("[RESET] %s: %lu seconds -> 0 (new day: %s)\n",
                       ip_str, clients[i].streaming_seconds, today);
            }

            /* Unblock if was blocked */
            if (clients[i].is_blocked) {
                unblock_client(clients[i].ip);
            }

            clients[i].streaming_seconds = 0;
            snprintf(clients[i].last_reset_date,
                     sizeof(clients[i].last_reset_date), "%s", today);
        }
    }
}

/* Normalize flow key - smaller IP first for consistent hashing */
static void normalize_flow_key(uint32_t *ip1, uint32_t *ip2,
                                uint16_t *port1, uint16_t *port2) {
    if (*ip1 > *ip2 || (*ip1 == *ip2 && *port1 > *port2)) {
        uint32_t tmp_ip = *ip1; *ip1 = *ip2; *ip2 = tmp_ip;
        uint16_t tmp_port = *port1; *port1 = *port2; *port2 = tmp_port;
    }
}

/* Simple hash for flow lookup */
static uint32_t flow_hash(uint32_t src_ip, uint32_t dst_ip,
                          uint16_t src_port, uint16_t dst_port,
                          uint8_t protocol) {
    /* Normalize so both directions hash the same */
    normalize_flow_key(&src_ip, &dst_ip, &src_port, &dst_port);
    uint32_t hash = src_ip ^ dst_ip ^ (src_port << 16) ^ dst_port ^ protocol;
    return hash % MAX_FLOWS;
}

/* Find or create flow */
static struct flow_info *get_flow(uint32_t src_ip, uint32_t dst_ip,
                                   uint16_t src_port, uint16_t dst_port,
                                   uint8_t protocol, int *is_new) {
    /* Normalize so both directions use same flow entry */
    uint32_t norm_ip1 = src_ip, norm_ip2 = dst_ip;
    uint16_t norm_port1 = src_port, norm_port2 = dst_port;
    normalize_flow_key(&norm_ip1, &norm_ip2, &norm_port1, &norm_port2);

    uint32_t idx = flow_hash(src_ip, dst_ip, src_port, dst_port, protocol);
    uint32_t start_idx = idx;
    *is_new = 0;

    /* Linear probing */
    do {
        struct flow_info *f = &flows[idx];

        if (!f->in_use) {
            /* New flow - store normalized */
            memset(f, 0, sizeof(*f));
            f->src_ip = norm_ip1;
            f->dst_ip = norm_ip2;
            f->src_port = norm_port1;
            f->dst_port = norm_port2;
            f->protocol = protocol;
            f->first_seen_ms = get_time_ms();
            f->last_seen_ms = f->first_seen_ms;
            f->in_use = 1;

            /* Allocate nDPI flow structure */
            f->ndpi_flow = ndpi_flow_malloc(SIZEOF_FLOW_STRUCT);
            if (f->ndpi_flow) memset(f->ndpi_flow, 0, SIZEOF_FLOW_STRUCT);

            *is_new = 1;
            return f;
        }

        /* Check if this is our flow (already normalized) */
        if (f->src_ip == norm_ip1 && f->dst_ip == norm_ip2 &&
            f->src_port == norm_port1 && f->dst_port == norm_port2 &&
            f->protocol == protocol) {
            f->last_seen_ms = get_time_ms();
            return f;
        }

        idx = (idx + 1) % MAX_FLOWS;
    } while (idx != start_idx);

    return NULL;  /* Table full */
}

/* Free flow resources */
static void free_flow(struct flow_info *f) {
    if (f->ndpi_flow) ndpi_flow_free(f->ndpi_flow);
    memset(f, 0, sizeof(*f));
}

/* Process expired flows */
static void expire_flows(void) {
    uint64_t now_ms = get_time_ms();

    for (int i = 0; i < MAX_FLOWS; i++) {
        struct flow_info *f = &flows[i];
        if (!f->in_use) continue;

        if ((now_ms - f->last_seen_ms) > (FLOW_IDLE_TIMEOUT * 1000)) {
            /* Flow expired - log if streaming */
            if (f->detection_completed && is_streaming_protocol(f->detected_protocol)) {
                char src_str[INET_ADDRSTRLEN], dst_str[INET_ADDRSTRLEN];
                struct in_addr src_addr = { .s_addr = f->src_ip };
                struct in_addr dst_addr = { .s_addr = f->dst_ip };
                inet_ntop(AF_INET, &src_addr, src_str, sizeof(src_str));
                inet_ntop(AF_INET, &dst_addr, dst_str, sizeof(dst_str));

                /* Duration in seconds (from milliseconds) */
                uint32_t duration_sec = (f->last_seen_ms - f->first_seen_ms) / 1000;
                char proto_name[64];
                ndpi_protocol2name(ndpi_module, f->detected_protocol, proto_name, sizeof(proto_name));

                printf("[FLOW_END] %s | %s:%d -> %s:%d | %lu bytes | %u sec\n",
                       proto_name, src_str, ntohs(f->src_port),
                       dst_str, ntohs(f->dst_port), f->bytes, duration_sec);

                /* Update client quota */
                uint32_t client_ip = is_lan_ip(f->src_ip) ? f->src_ip : f->dst_ip;
                int idx = get_client_index(client_ip);
                clients[idx].ip = client_ip;
                clients[idx].streaming_seconds += duration_sec;
                clients[idx].in_use = 1;

                /* Set today's date if not set */
                if (clients[idx].last_reset_date[0] == '\0') {
                    get_current_date(clients[idx].last_reset_date,
                                     sizeof(clients[idx].last_reset_date));
                }

                char client_str[INET_ADDRSTRLEN];
                struct in_addr client_addr = { .s_addr = client_ip };
                inet_ntop(AF_INET, &client_addr, client_str, sizeof(client_str));

                double pct = (100.0 * clients[idx].streaming_seconds) / daily_quota;
                printf("[QUOTA] %s | total: %lu/%lu seconds (%.1f min, %.0f%%)\n",
                       client_str, clients[idx].streaming_seconds, daily_quota,
                       clients[idx].streaming_seconds / 60.0, pct);

                /* Check if quota exceeded */
                if (clients[idx].streaming_seconds >= daily_quota) {
                    block_client(client_ip);
                }
            }

            free_flow(f);
        }
    }
}

/* Packet callback */
static void packet_handler(u_char *user, const struct pcap_pkthdr *header,
                           const u_char *packet) {
    (void)user;

    /* Skip ethernet header (14 bytes) */
    const struct ip *ip_header = (const struct ip *)(packet + 14);

    if (ip_header->ip_v != 4) return;  /* IPv4 only for now */

    uint32_t src_ip = ip_header->ip_src.s_addr;
    uint32_t dst_ip = ip_header->ip_dst.s_addr;
    uint8_t protocol = ip_header->ip_p;
    uint16_t src_port = 0, dst_port = 0;

    int ip_header_len = ip_header->ip_hl * 4;
    const u_char *transport = packet + 14 + ip_header_len;

    if (protocol == IPPROTO_TCP) {
        const struct tcphdr *tcp = (const struct tcphdr *)transport;
        src_port = tcp->th_sport;
        dst_port = tcp->th_dport;
    } else if (protocol == IPPROTO_UDP) {
        const struct udphdr *udp = (const struct udphdr *)transport;
        src_port = udp->uh_sport;
        dst_port = udp->uh_dport;
    } else {
        return;  /* Only TCP/UDP */
    }

    /* Skip if not LAN traffic */
    if (!is_lan_ip(src_ip) && !is_lan_ip(dst_ip)) return;

    /* Get or create flow */
    int is_new;
    struct flow_info *flow = get_flow(src_ip, dst_ip, src_port, dst_port, protocol, &is_new);
    if (!flow) return;

    flow->packets++;
    flow->bytes += header->len;

    /* Run nDPI detection if not completed */
    if (!flow->detection_completed && flow->ndpi_flow) {
        /* Get current time in milliseconds */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        uint64_t time_ms = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

        flow->detected_protocol = ndpi_detection_process_packet(
            ndpi_module,
            flow->ndpi_flow,
            (uint8_t *)ip_header,
            ntohs(ip_header->ip_len),
            time_ms
        );

        /* Check if detection is done */
        if (flow->detected_protocol.app_protocol != NDPI_PROTOCOL_UNKNOWN ||
            flow->packets > 10) {

            if (flow->detected_protocol.app_protocol == NDPI_PROTOCOL_UNKNOWN) {
                flow->detected_protocol = ndpi_detection_giveup(
                    ndpi_module, flow->ndpi_flow, 1, &flow->detection_completed);
            }
            flow->detection_completed = 1;

            /* Log new streaming flow */
            if (is_streaming_protocol(flow->detected_protocol)) {
                char src_str[INET_ADDRSTRLEN], dst_str[INET_ADDRSTRLEN];
                struct in_addr src_addr = { .s_addr = src_ip };
                struct in_addr dst_addr = { .s_addr = dst_ip };
                inet_ntop(AF_INET, &src_addr, src_str, sizeof(src_str));
                inet_ntop(AF_INET, &dst_addr, dst_str, sizeof(dst_str));

                char proto_name[64];
                ndpi_protocol2name(ndpi_module, flow->detected_protocol, proto_name, sizeof(proto_name));

                printf("[STREAMING] %s | %s:%d -> %s:%d\n",
                       proto_name, src_str, ntohs(src_port),
                       dst_str, ntohs(dst_port));
            }
        }
    }
}

/* Signal handler */
static void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

/* Print usage */
static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s -i <interface> [options]\n", prog);
    fprintf(stderr, "\nRequired:\n");
    fprintf(stderr, "  -i <iface>   Network interface (e.g., eth0, br-lan)\n");
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -s <subnet>  LAN subnet (default: %s)\n", LAN_SUBNET);
    fprintf(stderr, "  -m <mask>    LAN netmask (default: %s)\n", LAN_MASK);
    fprintf(stderr, "  -q <secs>    Daily quota in seconds (default: 3600)\n");
    fprintf(stderr, "  -f <file>    State file path (default: %s)\n", STATE_FILE);
    fprintf(stderr, "  -e           Enable enforcement mode (add blocked IPs to nftables)\n");
    fprintf(stderr, "  -h           Show this help\n");
    fprintf(stderr, "\nWithout -e, runs in dry-run mode (logs only, no blocking).\n");
    exit(1);
}

int main(int argc, char *argv[]) {
    char *interface = NULL;
    char *subnet = LAN_SUBNET;
    char *mask = LAN_MASK;
    char errbuf[PCAP_ERRBUF_SIZE];
    int opt;

    while ((opt = getopt(argc, argv, "i:s:m:q:f:eh")) != -1) {
        switch (opt) {
            case 'i': interface = optarg; break;
            case 's': subnet = optarg; break;
            case 'm': mask = optarg; break;
            case 'q': daily_quota = (uint64_t)atol(optarg); break;
            case 'f': state_file_path = optarg; break;
            case 'e': enforce_mode = 1; break;
            case 'h':
            default: usage(argv[0]);
        }
    }

    if (!interface) usage(argv[0]);

    /* Parse LAN network */
    inet_pton(AF_INET, subnet, &lan_network);
    inet_pton(AF_INET, mask, &lan_netmask);
    lan_network &= lan_netmask;

    printf("StreamGuard - Video Streaming Quota Enforcement\n");
    printf("Interface: %s | LAN: %s/%s | Quota: %lu sec | Mode: %s\n\n",
           interface, subnet, mask, daily_quota,
           enforce_mode ? "ENFORCE" : "DRY-RUN");

    /* Initialize nDPI */
    NDPI_PROTOCOL_BITMASK all;
    NDPI_BITMASK_SET_ALL(all);

    ndpi_module = ndpi_init_detection_module(ndpi_no_prefs);
    if (!ndpi_module) {
        fprintf(stderr, "Failed to initialize nDPI\n");
        return 1;
    }

    ndpi_set_protocol_detection_bitmask2(ndpi_module, &all);
    ndpi_finalize_initialization(ndpi_module);

    printf("nDPI initialized (version %s)\n", ndpi_revision());

    /* Load previous state and check for daily reset */
    load_state();
    check_daily_reset();

    /* Open pcap */
    pcap_handle = pcap_open_live(interface, 1600, 1, 100, errbuf);
    if (!pcap_handle) {
        fprintf(stderr, "pcap_open_live failed: %s\n", errbuf);
        return 1;
    }

    /* Set filter for HTTP/HTTPS/QUIC */
    struct bpf_program fp;
    if (pcap_compile(pcap_handle, &fp, "port 80 or port 443", 1, PCAP_NETMASK_UNKNOWN) == 0) {
        pcap_setfilter(pcap_handle, &fp);
        pcap_freecode(&fp);
    }

    printf("Capturing on %s... Press Ctrl+C to stop\n\n", interface);

    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Main loop */
    time_t last_expire = time(NULL);
    while (running) {
        struct pcap_pkthdr *header;
        const u_char *packet;

        int ret = pcap_next_ex(pcap_handle, &header, &packet);
        if (ret == 1) {
            packet_handler(NULL, header, packet);
        } else if (ret == -1) {
            fprintf(stderr, "pcap error: %s\n", pcap_geterr(pcap_handle));
            break;
        }

        /* Periodic maintenance */
        time_t now = time(NULL);
        if (now - last_expire >= 5) {
            expire_flows();
            check_daily_reset();
            last_expire = now;

            /* Save state periodically */
            if (now - last_state_save >= SAVE_INTERVAL) {
                save_state();
            }
        }
    }

    printf("\nShutting down...\n");

    /* Save final state */
    save_state();

    /* Final expiration to log remaining flows */
    for (int i = 0; i < MAX_FLOWS; i++) {
        if (flows[i].in_use) {
            flows[i].last_seen_ms = 0;  /* Force expiration */
        }
    }
    expire_flows();

    /* Print final quotas */
    printf("\n=== Final Quota Summary ===\n");
    for (int i = 0; i < 256; i++) {
        if (clients[i].in_use) {
            char ip_str[INET_ADDRSTRLEN];
            struct in_addr addr = { .s_addr = clients[i].ip };
            inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str));
            printf("%s: %lu seconds (%.1f minutes)\n",
                   ip_str, clients[i].streaming_seconds,
                   clients[i].streaming_seconds / 60.0);
        }
    }

    /* Cleanup */
    pcap_close(pcap_handle);
    ndpi_exit_detection_module(ndpi_module);

    return 0;
}
