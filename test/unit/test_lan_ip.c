/*
 * Unit tests for LAN IP detection
 */

#include <criterion/criterion.h>
#include <arpa/inet.h>
#include "streamguard_test.h"

static void setup(void) {
    /* Set up 192.168.0.0/24 network */
    struct in_addr network, netmask;
    inet_pton(AF_INET, "192.168.0.0", &network);
    inet_pton(AF_INET, "255.255.255.0", &netmask);
    test_set_lan_network(network.s_addr, netmask.s_addr);
}

TestSuite(lan_ip, .init = setup);

Test(lan_ip, local_ip_in_range) {
    struct in_addr addr;
    inet_pton(AF_INET, "192.168.0.100", &addr);
    cr_assert(is_lan_ip(addr.s_addr) == 1,
              "192.168.0.100 should be in LAN");
}

Test(lan_ip, local_ip_first) {
    struct in_addr addr;
    inet_pton(AF_INET, "192.168.0.1", &addr);
    cr_assert(is_lan_ip(addr.s_addr) == 1,
              "192.168.0.1 should be in LAN");
}

Test(lan_ip, local_ip_last) {
    struct in_addr addr;
    inet_pton(AF_INET, "192.168.0.254", &addr);
    cr_assert(is_lan_ip(addr.s_addr) == 1,
              "192.168.0.254 should be in LAN");
}

Test(lan_ip, network_address) {
    struct in_addr addr;
    inet_pton(AF_INET, "192.168.0.0", &addr);
    cr_assert(is_lan_ip(addr.s_addr) == 1,
              "192.168.0.0 (network address) should be in LAN");
}

Test(lan_ip, broadcast_address) {
    struct in_addr addr;
    inet_pton(AF_INET, "192.168.0.255", &addr);
    cr_assert(is_lan_ip(addr.s_addr) == 1,
              "192.168.0.255 (broadcast) should be in LAN");
}

Test(lan_ip, external_ip_google) {
    struct in_addr addr;
    inet_pton(AF_INET, "8.8.8.8", &addr);
    cr_assert(is_lan_ip(addr.s_addr) == 0,
              "8.8.8.8 should NOT be in LAN");
}

Test(lan_ip, external_ip_youtube) {
    struct in_addr addr;
    inet_pton(AF_INET, "142.250.1.1", &addr);
    cr_assert(is_lan_ip(addr.s_addr) == 0,
              "142.250.1.1 (YouTube) should NOT be in LAN");
}

Test(lan_ip, different_subnet) {
    struct in_addr addr;
    inet_pton(AF_INET, "192.168.1.100", &addr);
    cr_assert(is_lan_ip(addr.s_addr) == 0,
              "192.168.1.100 should NOT be in 192.168.0.0/24");
}

Test(lan_ip, private_10_net) {
    struct in_addr addr;
    inet_pton(AF_INET, "10.0.0.1", &addr);
    cr_assert(is_lan_ip(addr.s_addr) == 0,
              "10.0.0.1 should NOT be in 192.168.0.0/24");
}
