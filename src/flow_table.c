/*
 * Flow Table - uthash-based flow tracking for StreamGuard
 */

#include <stdlib.h>
#include <string.h>
#include "flow_table.h"

/* Initialize flow table */
void flow_table_init(struct flow_table *ft) {
    ft->entries = NULL;
    ft->count = 0;
}

/* Free all flows and cleanup */
void flow_table_free(struct flow_table *ft) {
    struct flow_entry *entry, *tmp;

    HASH_ITER(hh, ft->entries, entry, tmp) {
        HASH_DEL(ft->entries, entry);
        if (entry->ndpi_flow) {
            ndpi_flow_free(entry->ndpi_flow);
        }
        free(entry);
    }

    ft->entries = NULL;
    ft->count = 0;
}

/* Normalize flow key (smaller IP first for bidirectional matching) */
void flow_key_normalize(
    uint32_t *ip1, uint32_t *ip2,
    uint16_t *port1, uint16_t *port2
) {
    if (*ip1 > *ip2 || (*ip1 == *ip2 && *port1 > *port2)) {
        uint32_t tmp_ip = *ip1;
        *ip1 = *ip2;
        *ip2 = tmp_ip;

        uint16_t tmp_port = *port1;
        *port1 = *port2;
        *port2 = tmp_port;
    }
}

/* Find or create a flow entry */
struct flow_entry *flow_table_find_or_create(
    struct flow_table *ft,
    uint32_t src_ip, uint32_t dst_ip,
    uint16_t src_port, uint16_t dst_port,
    uint8_t protocol,
    uint64_t timestamp_ms,
    int *is_new
) {
    struct flow_key key;
    struct flow_entry *entry;

    /* Normalize key for bidirectional matching */
    uint32_t ip1 = src_ip, ip2 = dst_ip;
    uint16_t port1 = src_port, port2 = dst_port;
    flow_key_normalize(&ip1, &ip2, &port1, &port2);

    /* Build key */
    memset(&key, 0, sizeof(key));
    key.ip1 = ip1;
    key.ip2 = ip2;
    key.port1 = port1;
    key.port2 = port2;
    key.protocol = protocol;

    /* Look up existing entry */
    HASH_FIND(hh, ft->entries, &key, sizeof(key), entry);

    if (entry) {
        /* Found existing flow */
        entry->last_seen_ms = timestamp_ms;
        *is_new = 0;
        return entry;
    }

    /* Create new entry */
    entry = calloc(1, sizeof(*entry));
    if (!entry) {
        *is_new = 0;
        return NULL;
    }

    /* Set hash key */
    entry->key = key;

    /* Set compatibility fields (same values as key, for existing code) */
    entry->src_ip = ip1;
    entry->dst_ip = ip2;
    entry->src_port = port1;
    entry->dst_port = port2;
    entry->protocol = protocol;

    entry->first_seen_ms = timestamp_ms;
    entry->last_seen_ms = timestamp_ms;

    /* Allocate nDPI flow structure */
    entry->ndpi_flow = ndpi_flow_malloc(SIZEOF_FLOW_STRUCT);
    if (entry->ndpi_flow) {
        memset(entry->ndpi_flow, 0, SIZEOF_FLOW_STRUCT);
    }

    /* Add to hash table */
    HASH_ADD(hh, ft->entries, key, sizeof(key), entry);
    ft->count++;

    *is_new = 1;
    return entry;
}

/* Remove a specific flow from the table */
void flow_table_remove(struct flow_table *ft, struct flow_entry *entry) {
    if (!entry) return;

    HASH_DEL(ft->entries, entry);

    if (entry->ndpi_flow) {
        ndpi_flow_free(entry->ndpi_flow);
    }
    free(entry);

    ft->count--;
}

/* Expire flows older than timeout_ms */
void flow_table_expire(
    struct flow_table *ft,
    uint64_t now_ms,
    uint64_t timeout_ms,
    flow_expire_callback on_expire,
    void *user_data
) {
    struct flow_entry *entry, *tmp;

    HASH_ITER(hh, ft->entries, entry, tmp) {
        if ((now_ms - entry->last_seen_ms) > timeout_ms) {
            /* Call callback before removing */
            if (on_expire) {
                on_expire(entry, user_data);
            }

            /* Remove and free */
            HASH_DEL(ft->entries, entry);
            if (entry->ndpi_flow) {
                ndpi_flow_free(entry->ndpi_flow);
            }
            free(entry);
            ft->count--;
        }
    }
}
