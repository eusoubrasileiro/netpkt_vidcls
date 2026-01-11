/*
 * Firewall - SSH/firewall command execution for blocking/unblocking clients
 */

#ifndef FIREWALL_H
#define FIREWALL_H

#include <stdint.h>

/* Firewall configuration */
struct firewall_config {
    char *fw_router_ip;      /* Router IP for SSH commands (NULL for local) */
    char *fw_ssh_user;       /* SSH user (default: root) */
    char *fw_ssh_keyfile;    /* SSH key path (optional) */
    char *fw_type;           /* "nft" (nftables) or "ipset" (iptables) */
};

/* Execute firewall command via SSH (if router configured) or locally
 * Supports both nftables (fw4) and iptables/ipset (fw3) based on firewall_type
 * action: "add" or "del"
 * set_name: e.g., "blocked_streaming_clients" or "streaming_destinations"
 * ip_str: IP address string to add/remove
 * Returns: 0 on success, non-zero on failure
 */
int firewall_execute(const struct firewall_config *cfg,
                     const char *action, const char *set_name, const char *ip_str);

/* Add streaming destination IP to router's firewall set */
void firewall_add_destination(const struct firewall_config *cfg,
                              uint32_t dest_ip, int enforce_mode,
                              int (*is_lan_ip_fn)(uint32_t));

#endif /* FIREWALL_H */
