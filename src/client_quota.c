/*
 * Client Quota - Client tracking, session management, and quota enforcement
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include "client_quota.h"
#include "streamguard.h"
#include "log.h"

/* Get client index from IP (assumes /24 subnet) */
int cq_get_client_index(uint32_t ip) {
    return ntohl(ip) & 0xFF;  /* Extract last octet in host byte order */
}

/* Check if IP is in LAN */
int cq_is_lan_ip(uint32_t ip, uint32_t lan_network, uint32_t lan_netmask) {
    return (ip & lan_netmask) == lan_network;
}

/* Convert IP to string (uses static buffer - not thread safe) */
const char *cq_ip_to_str(uint32_t ip) {
    static char buf[INET_ADDRSTRLEN];
    struct in_addr addr = {.s_addr = ip};
    inet_ntop(AF_INET, &addr, buf, sizeof(buf));
    return buf;
}

/* Get current date as YYYY-MM-DD string */
void cq_get_current_date(char *buf, size_t len) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    strftime(buf, len, "%Y-%m-%d", tm);
}

/* Reset client session state */
void cq_reset_client_session(struct client_info *client) {
    client->session_start_ms = 0;
    client->last_streaming_activity_ms = 0;
}

/* Check if daily reset is needed for all clients */
void cq_check_daily_reset(struct client_info *clients, int max_clients,
                          void (*unblock_fn)(uint32_t ip)) {
    char today[16];
    cq_get_current_date(today, sizeof(today));

    for (int i = 0; i < max_clients; i++) {
        if (!clients[i].in_use) continue;

        /* If last_reset_date is empty or different from today, reset */
        if (clients[i].last_reset_date[0] == '\0' ||
            strcmp(clients[i].last_reset_date, today) != 0) {

            if (clients[i].streaming_seconds > 0) {
                log_info("RESET: %s: %lu seconds -> 0 (new day: %s)",
                         cq_ip_to_str(clients[i].ip), clients[i].streaming_seconds, today);
            }

            /* Unblock if was blocked */
            if (clients[i].is_blocked && unblock_fn) {
                unblock_fn(clients[i].ip);
            }

            clients[i].streaming_seconds = 0;
            snprintf(clients[i].last_reset_date,
                     sizeof(clients[i].last_reset_date), "%s", today);
        }
    }
}

/* Check for streaming session timeouts and quota violations */
void cq_check_session_timeouts(struct client_info *clients, int max_clients,
                               uint64_t now_ms, uint64_t daily_quota,
                               uint64_t session_timeout,
                               void (*block_fn)(uint32_t ip),
                               void (*retry_blocks_fn)(void)) {
    for (int i = 0; i < max_clients; i++) {
        if (!clients[i].in_use || clients[i].session_start_ms == 0)
            continue;

        if ((now_ms - clients[i].last_streaming_activity_ms) > session_timeout) {
            /* Session ended due to inactivity */

            /* Skip accumulation if already blocked - just reset session */
            if (clients[i].is_blocked) {
                cq_reset_client_session(&clients[i]);
                continue;
            }

            uint64_t duration_sec =
                (clients[i].last_streaming_activity_ms - clients[i].session_start_ms) / 1000;
            clients[i].streaming_seconds += duration_sec;

            double pct = (100.0 * clients[i].streaming_seconds) / daily_quota;
            log_info("SESSION_END: %s | +%lu sec | total: %lu/%lu sec (%.1f min, %.0f%%)",
                     cq_ip_to_str(clients[i].ip), duration_sec, clients[i].streaming_seconds, daily_quota,
                     clients[i].streaming_seconds / 60.0, pct);

            cq_reset_client_session(&clients[i]);

            /* Check if quota exceeded */
            if (clients[i].streaming_seconds >= daily_quota && block_fn) {
                block_fn(clients[i].ip);
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
                         cq_ip_to_str(clients[i].ip), clients[i].streaming_seconds, daily_quota);
                if (block_fn) {
                    block_fn(clients[i].ip);
                }
            }
        }
    }

    /* Retry any pending blocks */
    if (retry_blocks_fn) {
        retry_blocks_fn();
    }
}
