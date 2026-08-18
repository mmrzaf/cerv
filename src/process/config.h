#ifndef CERV_CONFIG_H
#define CERV_CONFIG_H

#include "base/bounds.h"
#include "base/cerv_time.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

struct cerv_config {
    struct sockaddr_storage listen_addr;
    socklen_t listen_addr_len;
    size_t workers;
    size_t max_connections;
    bool max_connections_auto;
    struct cerv_duration header_timeout;
    struct cerv_duration write_timeout;
    struct cerv_duration max_lifetime;
    struct cerv_duration shutdown_timeout;
    const char *root_path;
    unsigned char spa_fallback[CERV_PATH_BYTES_MAX];
    size_t spa_fallback_len;
    bool immutable;
};

struct cerv_config_env {
    const char *listen;
    const char *workers;
    const char *max_connections;
    const char *header_timeout;
    const char *write_timeout;
    const char *max_lifetime;
    const char *shutdown_timeout;
    const char *root;
    const char *immutable;
    const char *spa_fallback;
};

enum cerv_config_result {
    CERV_CONFIG_OK = 0,
    CERV_CONFIG_HELP,
    CERV_CONFIG_VERSION,
    CERV_CONFIG_ERROR
};

enum cerv_config_result cerv_config_parse(int argc, char *const argv[], struct cerv_config *out,
                                          char *error_buf, size_t error_cap);
enum cerv_config_result cerv_config_parse_with_env(int argc, char *const argv[], const struct cerv_config_env *env,
                                                   struct cerv_config *out, char *error_buf, size_t error_cap);
void cerv_config_env_read_process(struct cerv_config_env *out);
bool cerv_config_partition_slots(size_t total, size_t workers, size_t worker_index, size_t *out);
const char *cerv_config_help_text(void);
const char *cerv_version_text(void);

#endif
