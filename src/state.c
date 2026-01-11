/*
 * State - JSON persistence for client data
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <cjson/cJSON.h>
#include "state.h"
#include "streamguard.h"
#include "log.h"

/* Convert IP to string (local helper) */
static const char *ip_to_str_local(uint32_t ip) {
    static char buf[INET_ADDRSTRLEN];
    struct in_addr addr = {.s_addr = ip};
    inet_ntop(AF_INET, &addr, buf, sizeof(buf));
    return buf;
}

/* Save state to JSON file */
void state_save(struct client_info *clients, int max_clients,
                const char *state_file_path, time_t *last_state_save) {
    if (!state_file_path) return;  /* State persistence disabled */

    cJSON *root = cJSON_CreateObject();
    cJSON *clients_arr = cJSON_CreateArray();

    for (int i = 0; i < max_clients; i++) {
        if (!clients[i].in_use) continue;

        cJSON *client = cJSON_CreateObject();
        cJSON_AddStringToObject(client, "ip", ip_to_str_local(clients[i].ip));
        cJSON_AddNumberToObject(client, "streaming_seconds", clients[i].streaming_seconds);
        cJSON_AddStringToObject(client, "last_reset_date", clients[i].last_reset_date);
        cJSON_AddBoolToObject(client, "is_blocked", clients[i].is_blocked);
        cJSON_AddItemToArray(clients_arr, client);
    }

    cJSON_AddItemToObject(root, "clients", clients_arr);

    char *json_str = cJSON_Print(root);
    FILE *f = fopen(state_file_path, "w");
    if (f) {
        fputs(json_str, f);
        fclose(f);
    } else {
        log_warn("Could not save state to %s: %s", state_file_path, strerror(errno));
    }

    free(json_str);
    cJSON_Delete(root);
    if (last_state_save) {
        *last_state_save = time(NULL);
    }
}

/* Load state from JSON file */
void state_load(struct client_info *clients, int max_clients,
                const char *state_file_path,
                int (*get_client_index_fn)(uint32_t ip)) {
    if (!state_file_path) return;  /* State persistence disabled */

    FILE *f = fopen(state_file_path, "r");
    if (!f) {
        log_info("No previous state file found (%s), starting fresh", state_file_path);
        return;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *json_str = malloc(fsize + 1);
    if (!json_str) {
        fclose(f);
        return;
    }
    size_t bytes_read = fread(json_str, 1, fsize, f);
    json_str[bytes_read] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);

    if (!root) {
        log_warn("Could not parse state file");
        return;
    }

    cJSON *clients_arr = cJSON_GetObjectItem(root, "clients");
    if (!cJSON_IsArray(clients_arr)) {
        cJSON_Delete(root);
        return;
    }

    int loaded = 0;
    cJSON *client;
    cJSON_ArrayForEach(client, clients_arr) {
        cJSON *ip_json = cJSON_GetObjectItem(client, "ip");
        cJSON *seconds_json = cJSON_GetObjectItem(client, "streaming_seconds");
        cJSON *date_json = cJSON_GetObjectItem(client, "last_reset_date");
        cJSON *blocked_json = cJSON_GetObjectItem(client, "is_blocked");

        if (!cJSON_IsString(ip_json) || !cJSON_IsNumber(seconds_json)) continue;

        struct in_addr addr;
        if (inet_pton(AF_INET, ip_json->valuestring, &addr) != 1) continue;

        int idx = get_client_index_fn(addr.s_addr);
        if (idx < 0 || idx >= max_clients) continue;

        clients[idx].ip = addr.s_addr;
        clients[idx].streaming_seconds = (uint64_t)seconds_json->valuedouble;
        clients[idx].in_use = 1;

        if (cJSON_IsString(date_json)) {
            snprintf(clients[idx].last_reset_date,
                     sizeof(clients[idx].last_reset_date), "%s", date_json->valuestring);
        }
        if (cJSON_IsBool(blocked_json)) {
            clients[idx].is_blocked = cJSON_IsTrue(blocked_json);
        }
        loaded++;
    }

    cJSON_Delete(root);
    log_info("Loaded state for %d clients from %s", loaded, state_file_path);
}
