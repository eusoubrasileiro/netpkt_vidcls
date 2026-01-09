/*
 * StreamGuard Test Header
 *
 * Declares internal functions exposed for unit testing when
 * compiled with -DTESTING.
 */

#ifndef STREAMGUARD_TEST_H
#define STREAMGUARD_TEST_H

#include <stdint.h>
#include <ndpi_api.h>

/* Protocol detection functions */
int is_social_media_protocol(ndpi_protocol proto);
int is_trackable_traffic(ndpi_protocol proto);

/* Network utility functions */
int is_lan_ip(uint32_t ip);
int get_client_index(uint32_t ip);

/* Flow hashing functions */
uint32_t flow_hash(uint32_t src_ip, uint32_t dst_ip,
                   uint16_t src_port, uint16_t dst_port,
                   uint8_t protocol);
void normalize_flow_key(uint32_t *ip1, uint32_t *ip2,
                        uint16_t *port1, uint16_t *port2);

/* State persistence functions */
void save_state(void);
void load_state(void);

/* Test helper functions to set internal state */
void test_set_lan_network(uint32_t network, uint32_t netmask);
void test_set_video_only_mode(int mode);
void test_set_state_file_path(const char *path);
void test_init_client(int idx, uint32_t ip, uint64_t seconds, const char *date);
void test_get_client(int idx, uint32_t *ip, uint64_t *seconds, int *blocked);
int test_client_in_use(int idx);
void test_clear_clients(void);

#endif /* STREAMGUARD_TEST_H */
