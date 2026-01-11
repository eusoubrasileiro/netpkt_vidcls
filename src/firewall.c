/*
 * Firewall - SSH/firewall command execution for blocking/unblocking clients
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "firewall.h"
#include "log.h"

/* Convert IP to string (local helper) */
static const char *ip_to_str_local(uint32_t ip) {
    static char buf[INET_ADDRSTRLEN];
    struct in_addr addr = {.s_addr = ip};
    inet_ntop(AF_INET, &addr, buf, sizeof(buf));
    return buf;
}

/* Execute firewall command via SSH (if router configured) or locally */
int firewall_execute(const struct firewall_config *cfg,
                     const char *action, const char *set_name, const char *ip_str) {
    char fw_cmd[256];
    char cmd[512];

    /* Build firewall-specific command */
    if (strcmp(cfg->fw_type, "ipset") == 0) {
        /* iptables/ipset (fw3): ipset add/del set_name IP */
        snprintf(fw_cmd, sizeof(fw_cmd), "ipset %s %s %s 2>/dev/null",
                 action, set_name, ip_str);
    } else {
        /* nftables (fw4): nft add/delete element inet fw4 set_name '{ IP }' */
        const char *nft_action = (strcmp(action, "add") == 0) ? "add" : "delete";
        snprintf(fw_cmd, sizeof(fw_cmd),
                 "nft %s element inet fw4 %s '{ %s }' 2>/dev/null",
                 nft_action, set_name, ip_str);
    }

    /* Wrap with SSH if remote router configured */
    if (cfg->fw_router_ip != NULL) {
        if (cfg->fw_ssh_keyfile != NULL) {
            snprintf(cmd, sizeof(cmd),
                "ssh -o ConnectTimeout=5 -o BatchMode=yes -i %s %s@%s \"%s\"",
                cfg->fw_ssh_keyfile, cfg->fw_ssh_user, cfg->fw_router_ip, fw_cmd);
        } else {
            snprintf(cmd, sizeof(cmd),
                "ssh -o ConnectTimeout=5 -o BatchMode=yes %s@%s \"%s\"",
                cfg->fw_ssh_user, cfg->fw_router_ip, fw_cmd);
        }
    } else {
        snprintf(cmd, sizeof(cmd), "%s", fw_cmd);
    }

    log_debug("Executing: %s", cmd);
    return system(cmd);
}

/* Add streaming destination IP to router's firewall set */
void firewall_add_destination(const struct firewall_config *cfg,
                              uint32_t dest_ip, int enforce_mode,
                              int (*is_lan_ip_fn)(uint32_t)) {
    if (!enforce_mode || is_lan_ip_fn(dest_ip)) return;

    /* Duplicates silently ignored by both nft and ipset */
    firewall_execute(cfg, "add", "streaming_destinations", ip_to_str_local(dest_ip));
}
