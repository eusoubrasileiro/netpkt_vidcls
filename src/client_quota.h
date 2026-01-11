/*
 * Client Quota - Client tracking, session management, and quota enforcement
 */

#ifndef CLIENT_QUOTA_H
#define CLIENT_QUOTA_H

#include <stdint.h>

/* Forward declaration */
struct client_info;

/* Get client index from IP (assumes /24 subnet) */
int cq_get_client_index(uint32_t ip);

/* Check if IP is in LAN */
int cq_is_lan_ip(uint32_t ip, uint32_t lan_network, uint32_t lan_netmask);

/* Convert IP to string (uses static buffer - not thread safe) */
const char *cq_ip_to_str(uint32_t ip);

/* Get current date as YYYY-MM-DD string */
void cq_get_current_date(char *buf, size_t len);

/* Reset client session state */
void cq_reset_client_session(struct client_info *client);

/* Check if daily reset is needed for all clients
 * Calls unblock_fn for clients that need unblocking
 */
void cq_check_daily_reset(struct client_info *clients, int max_clients,
                          void (*unblock_fn)(uint32_t ip));

/* Check for streaming session timeouts and quota violations
 * now_ms: current time in milliseconds
 * daily_quota: quota in seconds
 * session_timeout: timeout in milliseconds
 * block_fn: function to call when quota exceeded
 * retry_blocks_fn: function to retry pending blocks
 */
void cq_check_session_timeouts(struct client_info *clients, int max_clients,
                               uint64_t now_ms, uint64_t daily_quota,
                               uint64_t session_timeout,
                               void (*block_fn)(uint32_t ip),
                               void (*retry_blocks_fn)(void));

#endif /* CLIENT_QUOTA_H */
