/*
 * State - JSON persistence for client data
 */

#ifndef STATE_H
#define STATE_H

#include <stdint.h>
#include <time.h>

/* Forward declaration */
struct client_info;

/* Save state to JSON file
 * clients: array of client_info structs
 * max_clients: size of the array
 * state_file_path: path to save to (NULL = skip save)
 * last_state_save: pointer to update with save time
 */
void state_save(struct client_info *clients, int max_clients,
                const char *state_file_path, time_t *last_state_save);

/* Load state from JSON file
 * clients: array of client_info structs to populate
 * max_clients: size of the array
 * state_file_path: path to load from (NULL = skip load)
 * get_client_index_fn: function to get index from IP
 */
void state_load(struct client_info *clients, int max_clients,
                const char *state_file_path,
                int (*get_client_index_fn)(uint32_t ip));

#endif /* STATE_H */
