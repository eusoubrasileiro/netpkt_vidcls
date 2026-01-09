/*
 * Unit tests for client index calculation
 */

#include <criterion/criterion.h>
#include <arpa/inet.h>
#include "streamguard_test.h"

Test(client_index, extracts_last_octet_100) {
    struct in_addr addr;
    inet_pton(AF_INET, "192.168.0.100", &addr);
    cr_assert_eq(get_client_index(addr.s_addr), 100,
                 "192.168.0.100 should have index 100");
}

Test(client_index, index_zero) {
    struct in_addr addr;
    inet_pton(AF_INET, "192.168.0.0", &addr);
    cr_assert_eq(get_client_index(addr.s_addr), 0,
                 "192.168.0.0 should have index 0");
}

Test(client_index, index_one) {
    struct in_addr addr;
    inet_pton(AF_INET, "192.168.0.1", &addr);
    cr_assert_eq(get_client_index(addr.s_addr), 1,
                 "192.168.0.1 should have index 1");
}

Test(client_index, index_255) {
    struct in_addr addr;
    inet_pton(AF_INET, "192.168.0.255", &addr);
    cr_assert_eq(get_client_index(addr.s_addr), 255,
                 "192.168.0.255 should have index 255");
}

Test(client_index, index_25) {
    struct in_addr addr;
    inet_pton(AF_INET, "192.168.0.25", &addr);
    cr_assert_eq(get_client_index(addr.s_addr), 25,
                 "192.168.0.25 should have index 25");
}

Test(client_index, index_122) {
    struct in_addr addr;
    inet_pton(AF_INET, "192.168.0.122", &addr);
    cr_assert_eq(get_client_index(addr.s_addr), 122,
                 "192.168.0.122 should have index 122");
}

Test(client_index, different_subnet_same_last_octet) {
    struct in_addr addr;
    inet_pton(AF_INET, "10.0.0.42", &addr);
    cr_assert_eq(get_client_index(addr.s_addr), 42,
                 "10.0.0.42 should have index 42 (last octet only)");
}

Test(client_index, external_ip) {
    struct in_addr addr;
    inet_pton(AF_INET, "142.250.1.138", &addr);
    cr_assert_eq(get_client_index(addr.s_addr), 138,
                 "142.250.1.138 should have index 138 (last octet)");
}
