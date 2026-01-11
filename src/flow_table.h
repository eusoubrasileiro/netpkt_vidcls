/*
 * Flow Table - uthash-based flow tracking for StreamGuard
 *
 * Replaces the custom linear-probing hash table with uthash,
 * providing correct deletion, dynamic sizing, and O(1) lookup.
 */

#ifndef FLOW_TABLE_H
#define FLOW_TABLE_H

#include <stdint.h>
#include <ndpi_api.h>
#include "uthash.h"

/* Flow key - 5-tuple (normalized: smaller IP first) */
struct flow_key {
    uint32_t ip1;       /* Lower IP (normalized) */
    uint32_t ip2;       /* Higher IP (normalized) */
    uint16_t port1;     /* Corresponding port */
    uint16_t port2;     /* Corresponding port */
    uint8_t protocol;   /* TCP=6, UDP=17 */
    uint8_t _pad[3];    /* Padding for alignment */
};

/*
 * Flow tracking structure
 * Uses same field names as old flow_info for compatibility
 */
struct flow_entry {
    /* Hash key stored separately for uthash */
    struct flow_key key;

    /* Original fields (matching flow_info layout for compatibility) */
    uint32_t src_ip;    /* Normalized IP1 */
    uint32_t dst_ip;    /* Normalized IP2 */
    uint16_t src_port;  /* Normalized port1 */
    uint16_t dst_port;  /* Normalized port2 */
    uint8_t protocol;

    struct ndpi_flow_struct *ndpi_flow;

    ndpi_protocol detected_protocol;
    uint8_t detection_completed;

    uint64_t bytes;
    uint32_t packets;
    uint64_t first_seen_ms;
    uint64_t last_seen_ms;

    /* uthash handle - must be last */
    UT_hash_handle hh;
};

/* Flow table structure */
struct flow_table {
    struct flow_entry *entries;  /* uthash head pointer */
    size_t count;                /* Number of active flows */
};

/* Initialize flow table */
void flow_table_init(struct flow_table *ft);

/* Free all flows and cleanup */
void flow_table_free(struct flow_table *ft);

/* Find or create a flow entry
 * Returns the flow entry (existing or newly created)
 * Sets *is_new to 1 if a new entry was created, 0 if existing
 * Returns NULL on allocation failure
 */
struct flow_entry *flow_table_find_or_create(
    struct flow_table *ft,
    uint32_t src_ip, uint32_t dst_ip,
    uint16_t src_port, uint16_t dst_port,
    uint8_t protocol,
    uint64_t timestamp_ms,
    int *is_new
);

/* Remove a specific flow from the table */
void flow_table_remove(struct flow_table *ft, struct flow_entry *entry);

/* Expire flows older than timeout_ms
 * Calls on_expire callback for each expired flow before removing
 * The callback can be NULL if no action is needed
 */
typedef void (*flow_expire_callback)(struct flow_entry *entry, void *user_data);
void flow_table_expire(
    struct flow_table *ft,
    uint64_t now_ms,
    uint64_t timeout_ms,
    flow_expire_callback on_expire,
    void *user_data
);

/* Get flow count */
static inline size_t flow_table_count(const struct flow_table *ft) {
    return ft->count;
}

/* Normalize flow key (smaller IP first for bidirectional matching) */
void flow_key_normalize(
    uint32_t *ip1, uint32_t *ip2,
    uint16_t *port1, uint16_t *port2
);

#endif /* FLOW_TABLE_H */
