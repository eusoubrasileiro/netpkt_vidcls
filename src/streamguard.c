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
#include <pcap/pcap.h>

/* Check if libpcap has remote capture support (rpcap://) */
#ifdef HAVE_REMOTE
#define RPCAP_ENABLED 1
#else
#define RPCAP_ENABLED 0
#endif
#include <ndpi_api.h>
#include <cjson/cJSON.h>
#include <sys/stat.h>
#include <errno.h>
#include "log.h"
#include "streamguard.h"

/* For unit testing: expose internal functions when compiled with -DTESTING */
#ifdef TESTING
#define STATIC  /* empty - expose functions for testing */
#else
#define STATIC static
#endif

/* Get current time in milliseconds (wall clock) */
static uint64_t get_wall_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Global context instance */
static struct streamguard_ctx g_ctx;

/* Initialize context with default values */
void streamguard_ctx_init(struct streamguard_ctx *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->running = 1;
    ctx->daily_quota = 3600;  /* 1 hour default */
    ctx->ssh_user = "root";
    ctx->firewall_type = "nft";
    flow_table_init(&ctx->flow_table);
}

/* Free context resources */
void streamguard_ctx_free(struct streamguard_ctx *ctx) {
    /* Free flow table (uthash-based) */
    flow_table_free(&ctx->flow_table);

    /* Close log file if open */
    if (ctx->log_file_fp) {
        fclose(ctx->log_file_fp);
        ctx->log_file_fp = NULL;
    }

    /* Note: pcap_handle and ndpi_module are freed in main() */
}

/* Globals - aliased to context for backward compatibility during migration */
#define ndpi_module     (g_ctx.ndpi_module)
#define g_flow_table    (g_ctx.flow_table)
#define clients         (g_ctx.clients)
#define pcap_handle     (g_ctx.pcap_handle)
#define running         (g_ctx.running)
#define lan_network     (g_ctx.lan_network)
#define lan_netmask     (g_ctx.lan_netmask)
#define enforce_mode    (g_ctx.enforce_mode)
#define debug_mode      (g_ctx.debug_mode)
#define video_only_mode (g_ctx.video_only_mode)
#define daily_quota     (g_ctx.daily_quota)
#define last_state_save (g_ctx.last_state_save)
#define state_file_path (g_ctx.state_file_path)
#define input_pcap      (g_ctx.input_pcap)
#define pcap_time_ms    (g_ctx.pcap_time_ms)
#define router_ip       (g_ctx.router_ip)
#define ssh_user        (g_ctx.ssh_user)
#define ssh_keyfile     (g_ctx.ssh_keyfile)
#define firewall_type   (g_ctx.firewall_type)
#define log_file_path   (g_ctx.log_file_path)
#define log_file_fp     (g_ctx.log_file_fp)

/* Not part of context - CLI argument only */
static char *unblock_ip = NULL;     /* -U: IP to unblock and exit */

/* Get current time - uses pcap timestamps when replaying, wall clock otherwise */
static uint64_t get_packet_time_ms(void) {
    if (input_pcap != NULL && pcap_time_ms > 0) {
        return pcap_time_ms;
    }
    return get_wall_time_ms();
}

/* Check if protocol is a social media platform (Instagram, Facebook) */
STATIC int is_social_media_protocol(ndpi_protocol proto) {
    uint16_t app = proto.proto.app_protocol;
    return (app == NDPI_PROTOCOL_INSTAGRAM ||
            app == NDPI_PROTOCOL_FACEBOOK ||
            app == NDPI_PROTOCOL_FACEBOOK_REEL_STORY);
}

/* Check if traffic should be tracked for quota
 * Default: VIDEO/STREAMING/MEDIA categories + social media (Instagram, Facebook)
 * Video-only mode (-V): Only VIDEO/STREAMING/MEDIA categories
 */
STATIC int is_trackable_traffic(ndpi_protocol proto) {
    /* Always track pure video/streaming categories */
    if (proto.category == NDPI_PROTOCOL_CATEGORY_VIDEO ||
        proto.category == NDPI_PROTOCOL_CATEGORY_STREAMING ||
        proto.category == NDPI_PROTOCOL_CATEGORY_MEDIA) {
        return 1;
    }
    /* Track social media unless in video-only mode */
    if (!video_only_mode && is_social_media_protocol(proto)) {
        return 1;
    }
    return 0;
}

/* Check if IP is in LAN */
STATIC int is_lan_ip(uint32_t ip) {
    return (ip & lan_netmask) == lan_network;
}

/* Convert IP to string (uses static buffer - not thread safe) */
static const char *ip_to_str(uint32_t ip) {
    static char buf[INET_ADDRSTRLEN];
    struct in_addr addr = {.s_addr = ip};
    inet_ntop(AF_INET, &addr, buf, sizeof(buf));
    return buf;
}

/* Get client index from IP (assumes /24) */
STATIC int get_client_index(uint32_t ip) {
    return ntohl(ip) & 0xFF;  /* Extract last octet in host byte order */
}

/* Reset client session state */
static inline void reset_client_session(int idx) {
    clients[idx].session_start_ms = 0;
    clients[idx].last_streaming_activity_ms = 0;
}

/* Get current date as YYYY-MM-DD string */
static void get_current_date(char *buf, size_t len) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    strftime(buf, len, "%Y-%m-%d", tm);
}

/* Execute firewall command via SSH (if router configured) or locally
 * Supports both nftables (fw4) and iptables/ipset (fw3) based on firewall_type
 * action: "add" or "del"
 * set_name: e.g., "blocked_streaming_clients" or "streaming_destinations"
 * ip: IP address to add/remove
 */
static int execute_firewall_command(const char *action, const char *set_name, const char *ip) {
    char fw_cmd[256];
    char cmd[512];

    /* Build firewall-specific command */
    if (strcmp(firewall_type, "ipset") == 0) {
        /* iptables/ipset (fw3): ipset add/del set_name IP */
        snprintf(fw_cmd, sizeof(fw_cmd), "ipset %s %s %s 2>/dev/null",
                 action, set_name, ip);
    } else {
        /* nftables (fw4): nft add/delete element inet fw4 set_name '{ IP }' */
        const char *nft_action = (strcmp(action, "add") == 0) ? "add" : "delete";
        snprintf(fw_cmd, sizeof(fw_cmd),
                 "nft %s element inet fw4 %s '{ %s }' 2>/dev/null",
                 nft_action, set_name, ip);
    }

    /* Wrap with SSH if remote router configured */
    if (router_ip != NULL) {
        if (ssh_keyfile != NULL) {
            snprintf(cmd, sizeof(cmd),
                "ssh -o ConnectTimeout=5 -o BatchMode=yes -i %s %s@%s \"%s\"",
                ssh_keyfile, ssh_user, router_ip, fw_cmd);
        } else {
            snprintf(cmd, sizeof(cmd),
                "ssh -o ConnectTimeout=5 -o BatchMode=yes %s@%s \"%s\"",
                ssh_user, router_ip, fw_cmd);
        }
    } else {
        snprintf(cmd, sizeof(cmd), "%s", fw_cmd);
    }

    log_debug("Executing: %s", cmd);
    return system(cmd);
}

/* Add streaming destination IP to router's firewall set */
static void add_streaming_destination(uint32_t dest_ip) {
    if (!enforce_mode || is_lan_ip(dest_ip)) return;

    /* Duplicates silently ignored by both nft and ipset */
    execute_firewall_command("add", "streaming_destinations", ip_to_str(dest_ip));
}

/* Block client via firewall (nftables or ipset) */
static void block_client(uint32_t client_ip) {
    const char *ip_str = ip_to_str(client_ip);
    int idx = get_client_index(client_ip);
    if (clients[idx].is_blocked) return;  /* Already blocked or pending retry */

    if (enforce_mode) {
        int ret = execute_firewall_command("add", "blocked_streaming_clients", ip_str);
        if (ret == 0) {
            clients[idx].is_blocked = 1;
            clients[idx].block_retry_after_ms = 0;  /* No retry needed */
            clients[idx].block_backoff_ms = 0;
            log_warn("BLOCKED: %s (quota exceeded: %lu/%lu seconds)%s",
                     ip_str, clients[idx].streaming_seconds, daily_quota,
                     router_ip ? " [remote]" : "");
        } else {
            /* Schedule retry with exponential backoff */
            uint64_t now_ms = get_wall_time_ms();
            uint32_t backoff = clients[idx].block_backoff_ms;
            if (backoff == 0) backoff = BLOCK_INITIAL_BACKOFF_MS;
            clients[idx].is_blocked = 1;  /* Prevent per-packet spam */
            clients[idx].block_retry_after_ms = now_ms + backoff;
            clients[idx].block_backoff_ms = NEXT_BACKOFF(backoff);
            log_error("Failed to block %s (%s returned %d), retry in %u sec",
                      ip_str, router_ip ? "SSH" : firewall_type, ret, backoff / 1000);
        }
    } else {
        log_info("DRY-RUN: Would block %s (quota exceeded: %lu/%lu seconds)",
                 ip_str, clients[idx].streaming_seconds, daily_quota);
    }
}

/* Unblock client via firewall (nftables or ipset) */
static void unblock_client(uint32_t client_ip) {
    const char *ip_str = ip_to_str(client_ip);
    int idx = get_client_index(client_ip);
    if (!clients[idx].is_blocked) return;  /* Not blocked */

    if (enforce_mode) {
        int ret = execute_firewall_command("del", "blocked_streaming_clients", ip_str);
        if (ret == 0) {
            clients[idx].is_blocked = 0;
            log_info("UNBLOCKED: %s (daily reset)%s",
                     ip_str, router_ip ? " [remote]" : "");
        } else {
            log_error("Failed to unblock %s (%s returned %d)",
                      ip_str, router_ip ? "SSH" : firewall_type, ret);
        }
    } else {
        log_info("DRY-RUN: Would unblock %s (daily reset)", ip_str);
    }
}

/* Retry pending blocks with exponential backoff */
static void retry_pending_blocks(void) {
    uint64_t now_ms = get_wall_time_ms();

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].in_use || !clients[i].is_blocked) continue;
        if (clients[i].block_retry_after_ms == 0) continue;  /* Already blocked */
        if (now_ms < clients[i].block_retry_after_ms) continue;  /* Not time yet */

        const char *ip_str = ip_to_str(clients[i].ip);
        int ret = execute_firewall_command("add", "blocked_streaming_clients", ip_str);

        if (ret == 0) {
            clients[i].block_retry_after_ms = 0;
            clients[i].block_backoff_ms = 0;
            log_warn("BLOCKED: %s (retry succeeded)%s",
                     ip_str, router_ip ? " [remote]" : "");
        } else {
            /* Schedule next retry with increased backoff */
            uint32_t backoff = clients[i].block_backoff_ms;
            clients[i].block_retry_after_ms = now_ms + backoff;
            clients[i].block_backoff_ms = NEXT_BACKOFF(backoff);
            log_error("Retry block %s failed (%s returned %d), next retry in %u sec",
                      ip_str, router_ip ? "SSH" : firewall_type, ret, backoff / 1000);
        }
    }
}

/* Save state to JSON file (only if -f specified) */
STATIC void save_state(void) {
    if (!state_file_path) return;  /* State persistence disabled */

    cJSON *root = cJSON_CreateObject();
    cJSON *clients_arr = cJSON_CreateArray();

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].in_use) continue;

        cJSON *client = cJSON_CreateObject();
        cJSON_AddStringToObject(client, "ip", ip_to_str(clients[i].ip));
        cJSON_AddNumberToObject(client, "streaming_seconds", clients[i].streaming_seconds);
        cJSON_AddStringToObject(client, "last_reset_date", clients[i].last_reset_date);
        cJSON_AddBoolToObject(client, "is_blocked", clients[i].is_blocked);
        cJSON_AddItemToArray(clients_arr, client);
    }

    cJSON_AddItemToObject(root, "clients", clients_arr);

    char *json_str = cJSON_Print(root);
    FILE *f = fopen(state_file_path, "w");
    if (f) {
        fputs(json_str, f);
        fclose(f);
    } else {
        log_warn("Could not save state to %s: %s", state_file_path, strerror(errno));
    }

    free(json_str);
    cJSON_Delete(root);
    last_state_save = time(NULL);
}

/* Load state from JSON file (only if -f specified) */
STATIC void load_state(void) {
    if (!state_file_path) return;  /* State persistence disabled */

    FILE *f = fopen(state_file_path, "r");
    if (!f) {
        log_info("No previous state file found (%s), starting fresh", state_file_path);
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
        log_warn("Could not parse state file");
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
    log_info("Loaded state for %d clients from %s", loaded, state_file_path);
}

/* Check if daily reset is needed */
static void check_daily_reset(void) {
    char today[16];
    get_current_date(today, sizeof(today));

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].in_use) continue;

        /* If last_reset_date is empty or different from today, reset */
        if (clients[i].last_reset_date[0] == '\0' ||
            strcmp(clients[i].last_reset_date, today) != 0) {

            if (clients[i].streaming_seconds > 0) {
                log_info("RESET: %s: %lu seconds -> 0 (new day: %s)",
                         ip_to_str(clients[i].ip), clients[i].streaming_seconds, today);
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
STATIC void normalize_flow_key(uint32_t *ip1, uint32_t *ip2,
                                uint16_t *port1, uint16_t *port2) {
    if (*ip1 > *ip2 || (*ip1 == *ip2 && *port1 > *port2)) {
        uint32_t tmp_ip = *ip1; *ip1 = *ip2; *ip2 = tmp_ip;
        uint16_t tmp_port = *port1; *port1 = *port2; *port2 = tmp_port;
    }
}

/* Simple hash for flow lookup */
STATIC uint32_t flow_hash(uint32_t src_ip, uint32_t dst_ip,
                          uint16_t src_port, uint16_t dst_port,
                          uint8_t protocol) {
    /* Normalize so both directions hash the same */
    normalize_flow_key(&src_ip, &dst_ip, &src_port, &dst_port);
    uint32_t hash = src_ip ^ dst_ip ^ (src_port << 16) ^ dst_port ^ protocol;
    return hash % MAX_FLOWS;
}

/* Find or create flow - wrapper around flow_table API */
static struct flow_entry *get_flow(uint32_t src_ip, uint32_t dst_ip,
                                   uint16_t src_port, uint16_t dst_port,
                                   uint8_t protocol, int *is_new) {
    return flow_table_find_or_create(&g_flow_table,
                                     src_ip, dst_ip,
                                     src_port, dst_port,
                                     protocol,
                                     get_packet_time_ms(),
                                     is_new);
}

/* Callback for flow expiration - logs streaming flows */
static void on_flow_expire(struct flow_entry *f, void *user_data) {
    (void)user_data;

    /* Log if streaming category (debug level) */
    if (f->detection_completed && is_trackable_traffic(f->detected_protocol)) {
        char src_str[INET_ADDRSTRLEN], dst_str[INET_ADDRSTRLEN];
        struct in_addr src_addr = { .s_addr = f->src_ip };
        struct in_addr dst_addr = { .s_addr = f->dst_ip };
        inet_ntop(AF_INET, &src_addr, src_str, sizeof(src_str));
        inet_ntop(AF_INET, &dst_addr, dst_str, sizeof(dst_str));

        uint64_t duration_ms = f->last_seen_ms - f->first_seen_ms;
        uint32_t duration_sec = duration_ms / 1000;
        char proto_name[64];
        ndpi_protocol2name(ndpi_module, f->detected_protocol.proto, proto_name, sizeof(proto_name));

        uint64_t bytes_per_sec = (duration_ms > 0) ? (f->bytes * 1000 / duration_ms) : 0;

        log_debug("FLOW_END: %s | %s:%d -> %s:%d | %lu bytes | %u sec | %lu KB/s",
                  proto_name, src_str, ntohs(f->src_port),
                  dst_str, ntohs(f->dst_port), f->bytes, duration_sec, bytes_per_sec / 1000);
    }
    /* Note: Quota is tracked via session-based timing, not flow duration */
}

/* Process expired flows */
static void expire_flows(void) {
    uint64_t now_ms = get_packet_time_ms();
    flow_table_expire(&g_flow_table, now_ms, FLOW_IDLE_TIMEOUT * 1000,
                      on_flow_expire, NULL);
}

/* Check for streaming session timeouts and quota violations */
static void check_session_timeouts(void) {
    uint64_t now_ms = get_packet_time_ms();

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].in_use || clients[i].session_start_ms == 0)
            continue;

        if ((now_ms - clients[i].last_streaming_activity_ms) > SESSION_TIMEOUT) {
            /* Session ended due to inactivity */

            /* Skip accumulation if already blocked - just reset session */
            if (clients[i].is_blocked) {
                reset_client_session(i);
                continue;
            }

            uint64_t duration_sec =
                (clients[i].last_streaming_activity_ms - clients[i].session_start_ms) / 1000;
            clients[i].streaming_seconds += duration_sec;

            double pct = (100.0 * clients[i].streaming_seconds) / daily_quota;
            log_info("SESSION_END: %s | +%lu sec | total: %lu/%lu sec (%.1f min, %.0f%%)",
                     ip_to_str(clients[i].ip), duration_sec, clients[i].streaming_seconds, daily_quota,
                     clients[i].streaming_seconds / 60.0, pct);

            reset_client_session(i);

            /* Check if quota exceeded */
            if (clients[i].streaming_seconds >= daily_quota) {
                block_client(clients[i].ip);
            }
        }
        /* Check quota during ACTIVE sessions (not just at timeout) */
        else if (!clients[i].is_blocked) {
            uint64_t session_duration_sec = (now_ms - clients[i].session_start_ms) / 1000;
            uint64_t effective_time = clients[i].streaming_seconds + session_duration_sec;

            if (effective_time >= daily_quota) {
                /* Accumulate session time and block immediately */
                clients[i].streaming_seconds = effective_time;
                clients[i].session_start_ms = now_ms;  /* Reset to avoid double-counting */

                log_warn("QUOTA_EXCEEDED: %s | %lu/%lu sec (blocked during active session)",
                         ip_to_str(clients[i].ip), clients[i].streaming_seconds, daily_quota);
                block_client(clients[i].ip);
            }
        }
    }

    /* Retry any pending blocks */
    retry_pending_blocks();
}

/* Packet callback */
static void packet_handler(u_char *user, const struct pcap_pkthdr *header,
                           const u_char *packet) {
    (void)user;

    /* Update pcap time from packet timestamp (for replay mode) */
    pcap_time_ms = (uint64_t)header->ts.tv_sec * 1000 + header->ts.tv_usec / 1000;

    /* Skip ethernet header */
    const struct ip *ip_header = (const struct ip *)(packet + ETHERNET_HEADER_SIZE);

    if (ip_header->ip_v != 4) return;  /* IPv4 only for now */

    uint32_t src_ip = ip_header->ip_src.s_addr;
    uint32_t dst_ip = ip_header->ip_dst.s_addr;
    uint8_t protocol = ip_header->ip_p;
    uint16_t src_port = 0, dst_port = 0;

    int ip_header_len = ip_header->ip_hl * 4;
    const u_char *transport = packet + ETHERNET_HEADER_SIZE + ip_header_len;

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
    struct flow_entry *flow = get_flow(src_ip, dst_ip, src_port, dst_port, protocol, &is_new);
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
            time_ms,
            NULL  /* input_info - nDPI 5.0 */
        );

        /* Check if detection is done
         * For QUIC: master_protocol is detected early, but we need more packets
         * to decrypt the Initial and get SNI for the app_protocol (YouTube, etc.)
         */
        uint16_t master = flow->detected_protocol.proto.master_protocol;
        uint16_t app = flow->detected_protocol.proto.app_protocol;

        /* QUIC needs more packets for sub-classification */
        int is_quic = (master == NDPI_PROTOCOL_QUIC || app == NDPI_PROTOCOL_QUIC);
        uint32_t max_packets = is_quic ? NDPI_QUIC_MAX_PACKETS : NDPI_DEFAULT_MAX_PACKETS;

        /* Detection complete when:
         * - App protocol identified (not just master), OR
         * - Max packets reached
         */
        int detection_done = (app != NDPI_PROTOCOL_UNKNOWN && app != NDPI_PROTOCOL_QUIC) ||
                             flow->packets > max_packets;

        if (detection_done) {
            if (app == NDPI_PROTOCOL_UNKNOWN || app == NDPI_PROTOCOL_QUIC) {
                flow->detected_protocol = ndpi_detection_giveup(
                    ndpi_module, flow->ndpi_flow);  /* nDPI 5.0: only 2 params */
            }
            flow->detection_completed = 1;

            /* Log detected protocol for debugging */
            char src_str[INET_ADDRSTRLEN], dst_str[INET_ADDRSTRLEN];
            struct in_addr src_addr = { .s_addr = src_ip };
            struct in_addr dst_addr = { .s_addr = dst_ip };
            inet_ntop(AF_INET, &src_addr, src_str, sizeof(src_str));
            inet_ntop(AF_INET, &dst_addr, dst_str, sizeof(dst_str));

            char proto_name[64];
            ndpi_protocol2name(ndpi_module, flow->detected_protocol.proto, proto_name, sizeof(proto_name));

            const char *cat_name = ndpi_category_get_name(ndpi_module, flow->detected_protocol.category);
            const char *proto_transport = (protocol == IPPROTO_UDP) ? "UDP" : "TCP";

            /* Get host info from nDPI flow */
            const char *host = (flow->ndpi_flow && flow->ndpi_flow->host_server_name[0])
                               ? (const char *)flow->ndpi_flow->host_server_name : "(none)";

            /* Log new streaming flow */
            if (is_trackable_traffic(flow->detected_protocol)) {
                log_info("STREAMING: %s (%s/%s) | %s:%d -> %s:%d | host=%s",
                         proto_name, proto_transport, cat_name, src_str, ntohs(src_port),
                         dst_str, ntohs(dst_port), host);

                /* Add streaming destination to router's nftables set */
                uint32_t stream_dst = is_lan_ip(src_ip) ? dst_ip : src_ip;
                add_streaming_destination(stream_dst);

                /* Update session tracking for this client */
                uint32_t client_ip = is_lan_ip(src_ip) ? src_ip : dst_ip;
                int idx = get_client_index(client_ip);
                uint64_t now_ms = get_packet_time_ms();

                clients[idx].ip = client_ip;
                clients[idx].in_use = 1;

                if (clients[idx].session_start_ms == 0) {
                    /* New streaming session starting */
                    clients[idx].session_start_ms = now_ms;
                    log_info("SESSION_START: %s", ip_to_str(client_ip));

                    /* Set today's date if not set */
                    if (clients[idx].last_reset_date[0] == '\0') {
                        get_current_date(clients[idx].last_reset_date,
                                         sizeof(clients[idx].last_reset_date));
                    }
                }
                clients[idx].last_streaming_activity_ms = now_ms;
            } else if (flow->detected_protocol.proto.app_protocol != NDPI_PROTOCOL_UNKNOWN) {
                /* Debug: log non-streaming detected protocols */
                log_debug("%s (%s/%s) | %s:%d -> %s:%d | host=%s",
                          proto_name, proto_transport, cat_name, src_str, ntohs(src_port),
                          dst_str, ntohs(dst_port), host);
            }
        }
    }

    /* Update session activity for ongoing streaming flows */
    if (flow->detection_completed && is_trackable_traffic(flow->detected_protocol)) {
        uint32_t client_ip = is_lan_ip(flow->src_ip) ? flow->src_ip : flow->dst_ip;
        int idx = get_client_index(client_ip);
        if (clients[idx].session_start_ms != 0) {
            clients[idx].last_streaming_activity_ms = get_packet_time_ms();
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
    fprintf(stderr, "Usage: %s [-i <interface> | -r <source>] [options]\n", prog);
    fprintf(stderr, "       %s -U <ip> -R <router> [-F ipset] [-k keyfile]\n", prog);
    fprintf(stderr, "\nInput (one required for monitoring mode):\n");
    fprintf(stderr, "  -i <iface>   Live capture from network interface (e.g., eth0, br-lan)\n");
    fprintf(stderr, "  -r <source>  Read from pcap file, stdin ('-'), or rpcap:// URL\n");
    fprintf(stderr, "               rpcap:// requires libpcap built with --enable-remote\n");
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -s <subnet>  LAN subnet (default: %s)\n", LAN_SUBNET);
    fprintf(stderr, "  -m <mask>    LAN netmask (default: %s)\n", LAN_MASK);
    fprintf(stderr, "  -q <secs>    Daily quota in seconds (default: 3600)\n");
    fprintf(stderr, "  -f <file>    Enable state persistence to file (disabled by default)\n");
    fprintf(stderr, "  -R <router>  Remote router IP for SSH-based blocking\n");
    fprintf(stderr, "  -u <user>    SSH user for remote router (default: root)\n");
    fprintf(stderr, "  -k <keyfile> SSH key file for remote router (uses default if omitted)\n");
    fprintf(stderr, "  -F <type>    Firewall type: 'nft' (nftables/fw4) or 'ipset' (iptables/fw3)\n");
    fprintf(stderr, "  -U <ip>      Unblock a client IP and exit (requires -R)\n");
    fprintf(stderr, "  -L <file>    Log to file (in addition to stderr)\n");
    fprintf(stderr, "  -e           Enable enforcement mode (add blocked IPs to firewall)\n");
    fprintf(stderr, "  -V           Video-only mode (ignore social media browsing)\n");
    fprintf(stderr, "  -d           Enable debug mode (verbose protocol logging)\n");
    fprintf(stderr, "  -h           Show this help\n");
    fprintf(stderr, "\nRemote capture via rpcapd (recommended):\n");
    fprintf(stderr, "  # Router runs rpcapd, Ubuntu captures and enforces:\n");
    fprintf(stderr, "  sudo ./streamguard -r rpcap://192.168.0.1/br-lan -e -R 192.168.0.1\n");
    fprintf(stderr, "\nRemote blocking (Ubuntu captures, OpenWrt blocks):\n");
    fprintf(stderr, "  # OpenWrt 22.03+ (fw4/nftables - default):\n");
    fprintf(stderr, "  sudo ./streamguard -i eno1 -e -R 192.168.0.1\n");
    fprintf(stderr, "  # OpenWrt 21.02 and older (fw3/iptables):\n");
    fprintf(stderr, "  sudo ./streamguard -i eno1 -e -R 192.168.0.1 -F ipset\n");
    fprintf(stderr, "\nUnblock a client:\n");
    fprintf(stderr, "  sudo ./streamguard -R 192.168.0.1 -F ipset -U 192.168.0.25\n");
    fprintf(stderr, "\nDefault: tracks all social media (Instagram, Facebook) + video streaming.\n");
    fprintf(stderr, "With -V: only tracks pure video streaming (YouTube, Netflix, TikTok).\n");
    fprintf(stderr, "Without -e, runs in dry-run mode (logs only, no blocking).\n");
    exit(1);
}

/* Test helper functions - only compiled when TESTING is defined */
#ifdef TESTING
void test_set_lan_network(uint32_t network, uint32_t netmask) {
    lan_network = network;
    lan_netmask = netmask;
}

void test_set_video_only_mode(int mode) {
    video_only_mode = mode;
}

void test_set_state_file_path(const char *path) {
    state_file_path = (char *)path;
}

void test_init_client(int idx, uint32_t ip, uint64_t seconds, const char *date) {
    if (idx < 0 || idx >= MAX_CLIENTS) return;
    clients[idx].ip = ip;
    clients[idx].streaming_seconds = seconds;
    clients[idx].in_use = 1;
    clients[idx].is_blocked = 0;
    if (date) {
        snprintf(clients[idx].last_reset_date, sizeof(clients[idx].last_reset_date), "%s", date);
    }
}

void test_get_client(int idx, uint32_t *ip, uint64_t *seconds, int *blocked) {
    if (idx < 0 || idx >= MAX_CLIENTS) return;
    if (ip) *ip = clients[idx].ip;
    if (seconds) *seconds = clients[idx].streaming_seconds;
    if (blocked) *blocked = clients[idx].is_blocked;
}

int test_client_in_use(int idx) {
    if (idx < 0 || idx >= MAX_CLIENTS) return 0;
    return clients[idx].in_use;
}

void test_clear_clients(void) {
    memset(clients, 0, sizeof(clients));
}
#endif

int main(int argc, char *argv[]) {
    char *interface = NULL;
    char *subnet = LAN_SUBNET;
    char *mask = LAN_MASK;
    char errbuf[PCAP_ERRBUF_SIZE];
    int opt;

    /* Initialize global context with defaults */
    streamguard_ctx_init(&g_ctx);

    while ((opt = getopt(argc, argv, "i:r:s:m:q:f:R:u:k:F:U:L:eVdh")) != -1) {
        switch (opt) {
            case 'i': interface = optarg; break;
            case 'r': input_pcap = optarg; break;
            case 's': subnet = optarg; break;
            case 'm': mask = optarg; break;
            case 'q': daily_quota = (uint64_t)atol(optarg); break;
            case 'f': state_file_path = optarg; break;
            case 'R': router_ip = optarg; break;
            case 'u': ssh_user = optarg; break;
            case 'k': ssh_keyfile = optarg; break;
            case 'F': firewall_type = optarg; break;
            case 'U': unblock_ip = optarg; break;
            case 'L': log_file_path = optarg; break;
            case 'e': enforce_mode = 1; break;
            case 'V': video_only_mode = 1; break;
            case 'd': debug_mode = 1; break;
            case 'h':
            default: usage(argv[0]);
        }
    }

    /* Initialize logger */
    log_set_level(debug_mode ? LOG_DEBUG : LOG_INFO);
    if (log_file_path) {
        log_file_fp = fopen(log_file_path, "a");
        if (log_file_fp) {
            log_add_fp(log_file_fp, LOG_TRACE);  /* All levels to file */
        } else {
            fprintf(stderr, "Warning: Could not open log file %s: %s\n",
                    log_file_path, strerror(errno));
        }
    }

    /* Handle unblock mode - unblock IP and exit */
    if (unblock_ip != NULL) {
        if (router_ip == NULL) {
            log_fatal("-U requires -R (router IP)");
            return 1;
        }
        struct in_addr addr;
        if (inet_pton(AF_INET, unblock_ip, &addr) != 1) {
            log_fatal("Invalid IP address: %s", unblock_ip);
            return 1;
        }
        enforce_mode = 1;  /* Enable to actually run command */
        int idx = get_client_index(addr.s_addr);
        clients[idx].ip = addr.s_addr;
        clients[idx].is_blocked = 1;  /* Set blocked so unblock_client works */
        clients[idx].in_use = 1;
        unblock_client(addr.s_addr);
        return 0;
    }

    if (!interface && !input_pcap) usage(argv[0]);
    if (interface && input_pcap) {
        log_fatal("Cannot use both -i and -r");
        usage(argv[0]);
    }

    /* Parse LAN network */
    inet_pton(AF_INET, subnet, &lan_network);
    inet_pton(AF_INET, mask, &lan_netmask);
    lan_network &= lan_netmask;

    log_info("StreamGuard - Video Streaming Quota Enforcement");
    const char *tracking_mode = video_only_mode ? "VIDEO-ONLY" : "ALL-SOCIAL";
    const char *exec_mode = enforce_mode ? "ENFORCE" : "DRY-RUN";
    if (input_pcap) {
        if (router_ip) {
            log_info("Input: %s | LAN: %s/%s | Quota: %lu sec | %s | %s | Router: %s@%s (%s)",
                     strcmp(input_pcap, "-") == 0 ? "stdin" : input_pcap,
                     subnet, mask, daily_quota, tracking_mode, exec_mode,
                     ssh_user, router_ip, firewall_type);
        } else {
            log_info("Input: %s | LAN: %s/%s | Quota: %lu sec | %s | %s",
                     strcmp(input_pcap, "-") == 0 ? "stdin" : input_pcap,
                     subnet, mask, daily_quota, tracking_mode, exec_mode);
        }
    } else {
        if (router_ip) {
            log_info("Interface: %s | LAN: %s/%s | Quota: %lu sec | %s | %s | Router: %s@%s (%s)",
                     interface, subnet, mask, daily_quota, tracking_mode, exec_mode,
                     ssh_user, router_ip, firewall_type);
        } else {
            log_info("Interface: %s | LAN: %s/%s | Quota: %lu sec | %s | %s",
                     interface, subnet, mask, daily_quota, tracking_mode, exec_mode);
        }
    }

    /* Initialize nDPI (5.0 API - all protocols enabled by default) */
    ndpi_module = ndpi_init_detection_module(NULL);
    if (!ndpi_module) {
        log_fatal("Failed to initialize nDPI");
        return 1;
    }

    ndpi_finalize_initialization(ndpi_module);

    log_info("nDPI initialized (version %s)", ndpi_revision());

    /* Load previous state and check for daily reset */
    load_state();
    check_daily_reset();

    /* Open pcap - rpcap:// URL, local file, stdin, or live interface */
    if (input_pcap) {
#if RPCAP_ENABLED
        if (strncmp(input_pcap, "rpcap://", 8) == 0) {
            /* Remote capture via rpcapd - requires libpcap built with --enable-remote */
            pcap_handle = pcap_open(input_pcap, PCAP_SNAPLEN, PCAP_OPENFLAG_PROMISCUOUS,
                                    PCAP_TIMEOUT_MS, NULL, errbuf);
            if (!pcap_handle) {
                log_fatal("pcap_open (rpcap) failed: %s", errbuf);
                return 1;
            }

            /* Set BPF filter for HTTP/HTTPS/QUIC */
            struct bpf_program fp;
            if (pcap_compile(pcap_handle, &fp, BPF_FILTER, 1, PCAP_NETMASK_UNKNOWN) == 0) {
                pcap_setfilter(pcap_handle, &fp);
                pcap_freecode(&fp);
            }

            log_info("Remote capture from %s...", input_pcap);
        } else
#else
        if (strncmp(input_pcap, "rpcap://", 8) == 0) {
            log_fatal("rpcap:// URLs require libpcap built with --enable-remote");
            return 1;
        }
#endif
        {
            /* Local file or stdin */
            pcap_handle = pcap_open_offline(input_pcap, errbuf);
            if (!pcap_handle) {
                log_fatal("pcap_open_offline failed: %s", errbuf);
                return 1;
            }
            log_info("Reading from %s...",
                     strcmp(input_pcap, "-") == 0 ? "stdin" : input_pcap);
        }
    } else {
        pcap_handle = pcap_open_live(interface, PCAP_SNAPLEN, 1, PCAP_TIMEOUT_MS, errbuf);
        if (!pcap_handle) {
            log_fatal("pcap_open_live failed: %s", errbuf);
            return 1;
        }

        /* Set filter for HTTP/HTTPS/QUIC (only for live capture) */
        struct bpf_program fp;
        if (pcap_compile(pcap_handle, &fp, BPF_FILTER, 1, PCAP_NETMASK_UNKNOWN) == 0) {
            pcap_setfilter(pcap_handle, &fp);
            pcap_freecode(&fp);
        }

        log_info("Capturing on %s... Press Ctrl+C to stop", interface);
    }

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
            /* In replay mode, check session timeouts after each packet */
            if (input_pcap != NULL) {
                check_session_timeouts();
            }
        } else if (ret == -2) {
            /* End of pcap file */
            break;
        } else if (ret == -1) {
            log_error("pcap error: %s", pcap_geterr(pcap_handle));
            break;
        }

        /* Periodic maintenance (live capture only) */
        if (input_pcap == NULL) {
            time_t now = time(NULL);
            if (now - last_expire >= 5) {
                expire_flows();
                check_session_timeouts();
                check_daily_reset();
                last_expire = now;

                /* Save state periodically */
                if (now - last_state_save >= SAVE_INTERVAL) {
                    save_state();
                }
            }
        }
    }

    log_info("Shutting down...");

    /* End any active streaming sessions */
    uint64_t now_ms = get_packet_time_ms();
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].in_use && clients[i].session_start_ms != 0) {
            /* Session was active - count time up to now */
            uint64_t duration_sec =
                (now_ms - clients[i].session_start_ms) / 1000;
            clients[i].streaming_seconds += duration_sec;

            log_info("SESSION_END: %s | +%lu sec (shutdown) | total: %lu sec",
                     ip_to_str(clients[i].ip), duration_sec, clients[i].streaming_seconds);

            clients[i].session_start_ms = 0;
        }
    }

    /* Save final state */
    save_state();

    /* Final expiration to log remaining flows (debug mode only) */
    /* Force all flows to expire by using a timestamp far in the future */
    flow_table_expire(&g_flow_table, now_ms + (FLOW_IDLE_TIMEOUT * 1000) + 1,
                      FLOW_IDLE_TIMEOUT * 1000, on_flow_expire, NULL);

    /* Print final quotas */
    log_info("=== Final Quota Summary ===");
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].in_use) {
            log_info("%s: %lu seconds (%.1f minutes)",
                     ip_to_str(clients[i].ip), clients[i].streaming_seconds,
                     clients[i].streaming_seconds / 60.0);
        }
    }

    /* Cleanup */
    pcap_close(pcap_handle);
    ndpi_exit_detection_module(ndpi_module);
    if (log_file_fp) fclose(log_file_fp);

    return 0;
}
