/*
 * Unit tests for state persistence (save/load JSON)
 */

#include <criterion/criterion.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include "streamguard_test.h"

static char temp_file[256];

static void setup(void) {
    /* Create unique temp file for each test */
    snprintf(temp_file, sizeof(temp_file), "/tmp/streamguard_test_%d.json", getpid());
    test_clear_clients();
}

static void teardown(void) {
    /* Clean up temp file */
    unlink(temp_file);
    test_set_state_file_path(NULL);
}

TestSuite(state, .init = setup, .fini = teardown);

/* Test save_state creates a valid JSON file */
Test(state, save_creates_file) {
    struct in_addr addr;
    inet_pton(AF_INET, "192.168.0.100", &addr);

    /* Initialize a client */
    test_init_client(100, addr.s_addr, 1234, "2026-01-09");
    test_set_state_file_path(temp_file);

    /* Save state */
    save_state();

    /* Verify file exists */
    FILE *f = fopen(temp_file, "r");
    cr_assert_not_null(f, "State file should be created");
    fclose(f);
}

/* Test save_state writes correct JSON content */
Test(state, save_writes_json) {
    struct in_addr addr;
    inet_pton(AF_INET, "192.168.0.100", &addr);

    test_init_client(100, addr.s_addr, 1234, "2026-01-09");
    test_set_state_file_path(temp_file);

    save_state();

    /* Read and verify JSON content */
    FILE *f = fopen(temp_file, "r");
    cr_assert_not_null(f, "State file should exist");

    char buffer[4096];
    size_t len = fread(buffer, 1, sizeof(buffer) - 1, f);
    buffer[len] = '\0';
    fclose(f);

    cr_assert(strstr(buffer, "192.168.0.100") != NULL,
              "JSON should contain IP address");
    cr_assert(strstr(buffer, "1234") != NULL,
              "JSON should contain streaming_seconds");
    cr_assert(strstr(buffer, "2026-01-09") != NULL,
              "JSON should contain last_reset_date");
}

/* Test load_state restores client data */
Test(state, load_restores_clients) {
    struct in_addr addr;
    inet_pton(AF_INET, "192.168.0.50", &addr);

    /* Create a state file manually */
    const char *json = "{\n"
        "  \"clients\": [\n"
        "    {\n"
        "      \"ip\": \"192.168.0.50\",\n"
        "      \"streaming_seconds\": 5000,\n"
        "      \"last_reset_date\": \"2026-01-08\",\n"
        "      \"is_blocked\": false\n"
        "    }\n"
        "  ]\n"
        "}";

    FILE *f = fopen(temp_file, "w");
    cr_assert_not_null(f, "Should be able to create temp file");
    fputs(json, f);
    fclose(f);

    test_set_state_file_path(temp_file);

    /* Load state */
    load_state();

    /* Verify client data restored */
    cr_assert(test_client_in_use(50) == 1, "Client 50 should be in use");

    uint32_t ip;
    uint64_t seconds;
    int blocked;
    test_get_client(50, &ip, &seconds, &blocked);

    cr_assert_eq(ip, addr.s_addr, "IP should match");
    cr_assert_eq(seconds, 5000, "Streaming seconds should be 5000");
    cr_assert_eq(blocked, 0, "Should not be blocked");
}

/* Test save/load round-trip preserves data */
Test(state, roundtrip) {
    struct in_addr addr1, addr2;
    inet_pton(AF_INET, "192.168.0.25", &addr1);
    inet_pton(AF_INET, "192.168.0.122", &addr2);

    /* Initialize two clients */
    test_init_client(25, addr1.s_addr, 3600, "2026-01-09");
    test_init_client(122, addr2.s_addr, 1800, "2026-01-09");
    test_set_state_file_path(temp_file);

    /* Save */
    save_state();

    /* Clear clients */
    test_clear_clients();
    cr_assert(test_client_in_use(25) == 0, "Client 25 should be cleared");
    cr_assert(test_client_in_use(122) == 0, "Client 122 should be cleared");

    /* Load */
    load_state();

    /* Verify both clients restored */
    cr_assert(test_client_in_use(25) == 1, "Client 25 should be restored");
    cr_assert(test_client_in_use(122) == 1, "Client 122 should be restored");

    uint64_t secs1, secs2;
    test_get_client(25, NULL, &secs1, NULL);
    test_get_client(122, NULL, &secs2, NULL);

    cr_assert_eq(secs1, 3600, "Client 25 should have 3600 seconds");
    cr_assert_eq(secs2, 1800, "Client 122 should have 1800 seconds");
}

/* Test load_state handles missing file gracefully */
Test(state, load_missing_file) {
    test_set_state_file_path("/tmp/nonexistent_file_12345.json");

    /* Should not crash */
    load_state();

    /* No clients should be loaded */
    cr_assert(test_client_in_use(0) == 0, "No clients should be loaded");
}

/* Test load_state handles invalid JSON gracefully */
Test(state, load_invalid_json) {
    /* Write invalid JSON */
    FILE *f = fopen(temp_file, "w");
    fputs("{ invalid json }", f);
    fclose(f);

    test_set_state_file_path(temp_file);

    /* Should not crash */
    load_state();

    /* No clients should be loaded */
    cr_assert(test_client_in_use(0) == 0, "No clients should be loaded from invalid JSON");
}

/* Test save_state with no state_file_path does nothing */
Test(state, save_no_path) {
    test_set_state_file_path(NULL);

    /* Should not crash or create file */
    save_state();

    /* Temp file should not exist */
    FILE *f = fopen(temp_file, "r");
    cr_assert_null(f, "No file should be created when path is NULL");
}
