#include "process/config.h"

#include "process/config_workers.h"

#include "base/bounds.h"
#include "base/checked.h"
#include "base/path.h"

#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


static const char cerv_help[] =
    "Usage: cerv [OPTIONS] [ROOT]\n"
    "\n"
    "Options:\n"
    "  --listen ADDRESS:PORT\n"
    "  --workers N|auto\n"
    "  --max-connections N|auto\n"
    "  --header-timeout DURATION\n"
    "  --write-timeout DURATION\n"
    "  --max-lifetime DURATION\n"
    "  --shutdown-timeout DURATION\n"
    "  --spa-fallback PATH\n"
    "  --immutable\n"
    "  --mutable\n"
    "  -h, --help\n"
    "  -V, --version\n"
    "\n"
    "Environment (CLI overrides environment):\n"
    "  CERV_LISTEN, CERV_WORKERS, CERV_MAX_CONNECTIONS, CERV_ROOT\n"
    "  CERV_HEADER_TIMEOUT, CERV_WRITE_TIMEOUT, CERV_MAX_LIFETIME\n"
    "  CERV_SHUTDOWN_TIMEOUT, CERV_IMMUTABLE, CERV_SPA_FALLBACK\n";

#ifndef CERV_VERSION
#error "CERV_VERSION must be provided by the build"
#endif

static const char cerv_version[] = "cerv " CERV_VERSION "\n";

const char *cerv_config_help_text(void) { return cerv_help; }
const char *cerv_version_text(void) { return cerv_version; }

static void cerv_config_error(char *buf, size_t cap, const char *message)
{
    if (buf == NULL || cap == 0U) return;
    (void)snprintf(buf, cap, "%s", message);
}

static bool cerv_parse_u64_text(const char *text, uint64_t *out)
{
    size_t len;
    if (text == NULL) return false;
    len = strlen(text);
    return cerv_u64_decimal((const unsigned char *)text, len, out);
}

static bool cerv_parse_size_positive(const char *text, size_t *out)
{
    uint64_t value;
    if (out == NULL || !cerv_parse_u64_text(text, &value) || value == UINT64_C(0) || value > (uint64_t)SIZE_MAX) {
        return false;
    }
    *out = (size_t)value;
    return true;
}

static bool cerv_parse_duration(const char *text, struct cerv_duration *out)
{
    size_t len;
    size_t digits;
    uint64_t value;
    uint64_t ms;
    if (text == NULL || out == NULL) return false;
    len = strlen(text);
    if (len < 2U) return false;
    if (len >= 3U && text[len - 2U] == 'm' && text[len - 1U] == 's') {
        digits = len - 2U;
        if (!cerv_u64_decimal((const unsigned char *)text, digits, &value) || value == UINT64_C(0)) return false;
        ms = value;
    } else if (text[len - 1U] == 's') {
        digits = len - 1U;
        if (!cerv_u64_decimal((const unsigned char *)text, digits, &value) || value == UINT64_C(0) ||
            !cerv_u64_mul(value, UINT64_C(1000), &ms)) return false;
    } else if (text[len - 1U] == 'm') {
        digits = len - 1U;
        if (!cerv_u64_decimal((const unsigned char *)text, digits, &value) || value == UINT64_C(0) ||
            !cerv_u64_mul(value, UINT64_C(60000), &ms)) return false;
    } else {
        return false;
    }
    return cerv_duration_from_ms(ms, out) && out->ns != UINT64_C(0);
}

static bool cerv_ascii_ci_equal(const char *text, const char *literal)
{
    size_t i = 0U;
    if (text == NULL || literal == NULL) return false;
    while (text[i] != '\0' && literal[i] != '\0') {
        unsigned char a = (unsigned char)text[i];
        unsigned char b = (unsigned char)literal[i];
        if (a >= (unsigned char)'A' && a <= (unsigned char)'Z') a = (unsigned char)(a + 32U);
        if (b >= (unsigned char)'A' && b <= (unsigned char)'Z') b = (unsigned char)(b + 32U);
        if (a != b) return false;
        ++i;
    }
    return text[i] == '\0' && literal[i] == '\0';
}

static bool cerv_parse_bool(const char *text, bool *out)
{
    if (text == NULL || out == NULL || text[0] == '\0') return false;
    if (strcmp(text, "1") == 0 || cerv_ascii_ci_equal(text, "true") || cerv_ascii_ci_equal(text, "yes") ||
        cerv_ascii_ci_equal(text, "on")) {
        *out = true;
        return true;
    }
    if (strcmp(text, "0") == 0 || cerv_ascii_ci_equal(text, "false") || cerv_ascii_ci_equal(text, "no") ||
        cerv_ascii_ci_equal(text, "off")) {
        *out = false;
        return true;
    }
    return false;
}

static bool cerv_segment_dot_or_dotdot(const unsigned char *bytes, size_t len)
{
    return (len == 1U && bytes[0] == (unsigned char)'.') ||
           (len == 2U && bytes[0] == (unsigned char)'.' && bytes[1] == (unsigned char)'.');
}

static bool cerv_parse_spa_fallback(const char *text, unsigned char out[CERV_PATH_BYTES_MAX], size_t *out_len)
{
    size_t len = 0U;
    size_t in_pos;
    size_t write_pos = 0U;
    size_t segment_start = 0U;
    if (text == NULL || out == NULL || out_len == NULL) return false;
    while (len < CERV_PATH_BYTES_MAX && text[len] != '\0') ++len;
    if (len == 0U || len >= CERV_PATH_BYTES_MAX) return false;
    in_pos = text[0] == '/' ? 1U : 0U;
    if (in_pos == len) return false;
    for (; in_pos < len; ++in_pos) {
        unsigned char c = (unsigned char)text[in_pos];
        if (c == (unsigned char)'/' ) {
            if (write_pos == segment_start || cerv_segment_dot_or_dotdot(out + segment_start, write_pos - segment_start)) {
                return false;
            }
            if (write_pos >= CERV_PATH_BYTES_MAX - 1U) return false;
            out[write_pos++] = c;
            segment_start = write_pos;
            continue;
        }
        if (c == (unsigned char)'\\' || c == (unsigned char)'?' || c == (unsigned char)'#' ||
            c < 0x20U || c == 0x7fU) return false;
        if (write_pos >= CERV_PATH_BYTES_MAX - 1U) return false;
        out[write_pos++] = c;
    }
    if (write_pos == segment_start || cerv_segment_dot_or_dotdot(out + segment_start, write_pos - segment_start)) return false;
    /* A hidden fallback could never be served, so refuse it at startup instead of failing every request. */
    if (cerv_path_is_hidden(out, write_pos)) return false;
    out[write_pos] = 0U;
    *out_len = write_pos;
    return true;
}

void cerv_config_env_read_process(struct cerv_config_env *out)
{
    if (out == NULL) return;
    *out = (struct cerv_config_env){
        .listen = getenv("CERV_LISTEN"),
        .workers = getenv("CERV_WORKERS"),
        .max_connections = getenv("CERV_MAX_CONNECTIONS"),
        .header_timeout = getenv("CERV_HEADER_TIMEOUT"),
        .write_timeout = getenv("CERV_WRITE_TIMEOUT"),
        .max_lifetime = getenv("CERV_MAX_LIFETIME"),
        .shutdown_timeout = getenv("CERV_SHUTDOWN_TIMEOUT"),
        .root = getenv("CERV_ROOT"),
        .immutable = getenv("CERV_IMMUTABLE"),
        .spa_fallback = getenv("CERV_SPA_FALLBACK")
    };
}

static bool cerv_parse_port(const char *text, uint16_t *out)
{
    uint64_t port;
    if (!cerv_parse_u64_text(text, &port) || port == UINT64_C(0) || port > UINT64_C(65535) || out == NULL) return false;
    *out = (uint16_t)port;
    return true;
}

static bool cerv_parse_listen(const char *text, struct sockaddr_storage *storage, socklen_t *storage_len)
{
    char host[INET6_ADDRSTRLEN + 1U];
    const char *port_text;
    size_t host_len;
    uint16_t port;
    if (text == NULL || storage == NULL || storage_len == NULL) return false;
    memset(storage, 0, sizeof(*storage));
    if (text[0] == '[') {
        const char *close = strchr(text + 1, ']');
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)(void *)storage;
        if (close == NULL || close[1] != ':' || close[2] == '\0') return false;
        host_len = (size_t)(close - (text + 1));
        if (host_len == 0U || host_len >= sizeof(host)) return false;
        memcpy(host, text + 1, host_len);
        host[host_len] = '\0';
        port_text = close + 2;
        if (!cerv_parse_port(port_text, &port) || inet_pton(AF_INET6, host, &addr6->sin6_addr) != 1) return false;
        addr6->sin6_family = AF_INET6;
        addr6->sin6_port = htons(port);
        *storage_len = (socklen_t)sizeof(*addr6);
        return true;
    }
    {
        const char *colon = strrchr(text, ':');
        struct sockaddr_in *addr4 = (struct sockaddr_in *)(void *)storage;
        if (colon == NULL || colon == text || strchr(text, ':') != colon || colon[1] == '\0') return false;
        host_len = (size_t)(colon - text);
        if (host_len >= sizeof(host)) return false;
        memcpy(host, text, host_len);
        host[host_len] = '\0';
        port_text = colon + 1;
        if (!cerv_parse_port(port_text, &port) || inet_pton(AF_INET, host, &addr4->sin_addr) != 1) return false;
        addr4->sin_family = AF_INET;
        addr4->sin_port = htons(port);
        *storage_len = (socklen_t)sizeof(*addr4);
        return true;
    }
}


bool cerv_config_partition_slots(size_t total, size_t workers, size_t worker_index, size_t *out)
{
    size_t base;
    size_t extra;
    if (out == NULL || total == 0U || workers == 0U || worker_index >= workers || workers > total) return false;
    base = total / workers;
    extra = total % workers;
    *out = base + (worker_index < extra ? 1U : 0U);
    return true;
}

static bool cerv_option_value(int argc, char *const argv[], int *index, const char *arg,
                              const char *name, const char **value)
{
    size_t name_len = strlen(name);
    if (strncmp(arg, name, name_len) != 0) return false;
    if (arg[name_len] == '=') {
        if (arg[name_len + 1U] == '\0') return false;
        *value = arg + name_len + 1U;
        return true;
    }
    if (arg[name_len] != '\0' || *index + 1 >= argc) return false;
    ++*index;
    *value = argv[*index];
    return true;
}

enum cerv_config_result cerv_config_parse_with_env(int argc, char *const argv[], const struct cerv_config_env *env,
                                                   struct cerv_config *out, char *error_buf, size_t error_cap)
{
    bool seen_listen = false;
    bool seen_workers = false;
    bool seen_max = false;
    bool seen_header = false;
    bool seen_write = false;
    bool seen_lifetime = false;
    bool seen_shutdown = false;
    bool seen_immutable = false;
    bool seen_spa_fallback = false;
    bool workers_auto = true;
    int i;
    if (out == NULL || argc < 1 || argv == NULL) return CERV_CONFIG_ERROR;
    memset(out, 0, sizeof(*out));
    out->max_connections = CERV_DEFAULT_MAX_CONNECTIONS;
    out->max_connections_auto = true;
    if (!cerv_duration_from_ms(CERV_DEFAULT_HEADER_TIMEOUT_MS, &out->header_timeout) ||
        !cerv_duration_from_ms(CERV_DEFAULT_WRITE_TIMEOUT_MS, &out->write_timeout) ||
        !cerv_duration_from_ms(CERV_DEFAULT_MAX_LIFETIME_MS, &out->max_lifetime) ||
        !cerv_duration_from_ms(CERV_DEFAULT_SHUTDOWN_TIMEOUT_MS, &out->shutdown_timeout)) return CERV_CONFIG_ERROR;
    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        const char *value = NULL;
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) return CERV_CONFIG_HELP;
        if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0) return CERV_CONFIG_VERSION;
        if (strcmp(arg, "--immutable") == 0) {
            if (seen_immutable) { cerv_config_error(error_buf, error_cap, "duplicate --immutable"); return CERV_CONFIG_ERROR; }
            seen_immutable = true;
            out->immutable = true;
            continue;
        }
        if (strcmp(arg, "--mutable") == 0) {
            if (seen_immutable) { cerv_config_error(error_buf, error_cap, "duplicate cache policy"); return CERV_CONFIG_ERROR; }
            seen_immutable = true;
            out->immutable = false;
            continue;
        }
        if (arg[0] == '-') {
            if (cerv_option_value(argc, argv, &i, arg, "--listen", &value)) {
                if (seen_listen || !cerv_parse_listen(value, &out->listen_addr, &out->listen_addr_len)) {
                    cerv_config_error(error_buf, error_cap, seen_listen ? "duplicate --listen" : "invalid --listen");
                    return CERV_CONFIG_ERROR;
                }
                seen_listen = true;
            } else if (cerv_option_value(argc, argv, &i, arg, "--workers", &value)) {
                if (seen_workers) { cerv_config_error(error_buf, error_cap, "duplicate --workers"); return CERV_CONFIG_ERROR; }
                seen_workers = true;
                if (strcmp(value, "auto") == 0) workers_auto = true;
                else {
                    workers_auto = false;
                    if (!cerv_parse_size_positive(value, &out->workers) || out->workers > CERV_WORKERS_MAX) {
                        cerv_config_error(error_buf, error_cap, "invalid --workers"); return CERV_CONFIG_ERROR;
                    }
                }
            } else if (cerv_option_value(argc, argv, &i, arg, "--max-connections", &value)) {
                if (seen_max) { cerv_config_error(error_buf, error_cap, "duplicate --max-connections"); return CERV_CONFIG_ERROR; }
                seen_max = true;
                if (strcmp(value, "auto") == 0) out->max_connections_auto = true;
                else {
                    out->max_connections_auto = false;
                    if (!cerv_parse_size_positive(value, &out->max_connections) || out->max_connections >= (size_t)UINT32_MAX) {
                        cerv_config_error(error_buf, error_cap, "invalid --max-connections"); return CERV_CONFIG_ERROR;
                    }
                }
            } else if (cerv_option_value(argc, argv, &i, arg, "--header-timeout", &value)) {
                if (seen_header || !cerv_parse_duration(value, &out->header_timeout)) {
                    cerv_config_error(error_buf, error_cap, seen_header ? "duplicate --header-timeout" : "invalid --header-timeout"); return CERV_CONFIG_ERROR;
                }
                seen_header = true;
            } else if (cerv_option_value(argc, argv, &i, arg, "--write-timeout", &value)) {
                if (seen_write || !cerv_parse_duration(value, &out->write_timeout)) {
                    cerv_config_error(error_buf, error_cap, seen_write ? "duplicate --write-timeout" : "invalid --write-timeout"); return CERV_CONFIG_ERROR;
                }
                seen_write = true;
            } else if (cerv_option_value(argc, argv, &i, arg, "--max-lifetime", &value)) {
                if (seen_lifetime || !cerv_parse_duration(value, &out->max_lifetime)) {
                    cerv_config_error(error_buf, error_cap, seen_lifetime ? "duplicate --max-lifetime" : "invalid --max-lifetime"); return CERV_CONFIG_ERROR;
                }
                seen_lifetime = true;
            } else if (cerv_option_value(argc, argv, &i, arg, "--shutdown-timeout", &value)) {
                if (seen_shutdown || !cerv_parse_duration(value, &out->shutdown_timeout)) {
                    cerv_config_error(error_buf, error_cap, seen_shutdown ? "duplicate --shutdown-timeout" : "invalid --shutdown-timeout"); return CERV_CONFIG_ERROR;
                }
                seen_shutdown = true;
            } else if (cerv_option_value(argc, argv, &i, arg, "--spa-fallback", &value)) {
                if (seen_spa_fallback || !cerv_parse_spa_fallback(value, out->spa_fallback, &out->spa_fallback_len)) {
                    cerv_config_error(error_buf, error_cap, seen_spa_fallback ? "duplicate --spa-fallback" : "invalid --spa-fallback");
                    return CERV_CONFIG_ERROR;
                }
                seen_spa_fallback = true;
            } else {
                cerv_config_error(error_buf, error_cap, "unknown or incomplete option");
                return CERV_CONFIG_ERROR;
            }
            continue;
        }
        if (out->root_path != NULL) { cerv_config_error(error_buf, error_cap, "multiple ROOT arguments"); return CERV_CONFIG_ERROR; }
        if (arg[0] == '\0') { cerv_config_error(error_buf, error_cap, "empty ROOT"); return CERV_CONFIG_ERROR; }
        out->root_path = arg;
    }

    if (!seen_listen && env != NULL && env->listen != NULL) {
        if (!cerv_parse_listen(env->listen, &out->listen_addr, &out->listen_addr_len)) {
            cerv_config_error(error_buf, error_cap, "invalid CERV_LISTEN"); return CERV_CONFIG_ERROR;
        }
        seen_listen = true;
    }
    if (!seen_workers && env != NULL && env->workers != NULL) {
        if (strcmp(env->workers, "auto") == 0) workers_auto = true;
        else {
            workers_auto = false;
            if (!cerv_parse_size_positive(env->workers, &out->workers) || out->workers > CERV_WORKERS_MAX) {
                cerv_config_error(error_buf, error_cap, "invalid CERV_WORKERS"); return CERV_CONFIG_ERROR;
            }
        }
    }
    if (!seen_max && env != NULL && env->max_connections != NULL) {
        if (strcmp(env->max_connections, "auto") == 0) out->max_connections_auto = true;
        else {
            out->max_connections_auto = false;
            if (!cerv_parse_size_positive(env->max_connections, &out->max_connections) ||
                out->max_connections >= (size_t)UINT32_MAX) {
                cerv_config_error(error_buf, error_cap, "invalid CERV_MAX_CONNECTIONS"); return CERV_CONFIG_ERROR;
            }
        }
    }
    if (!seen_header && env != NULL && env->header_timeout != NULL &&
        !cerv_parse_duration(env->header_timeout, &out->header_timeout)) {
        cerv_config_error(error_buf, error_cap, "invalid CERV_HEADER_TIMEOUT"); return CERV_CONFIG_ERROR;
    }
    if (!seen_write && env != NULL && env->write_timeout != NULL &&
        !cerv_parse_duration(env->write_timeout, &out->write_timeout)) {
        cerv_config_error(error_buf, error_cap, "invalid CERV_WRITE_TIMEOUT"); return CERV_CONFIG_ERROR;
    }
    if (!seen_lifetime && env != NULL && env->max_lifetime != NULL &&
        !cerv_parse_duration(env->max_lifetime, &out->max_lifetime)) {
        cerv_config_error(error_buf, error_cap, "invalid CERV_MAX_LIFETIME"); return CERV_CONFIG_ERROR;
    }
    if (!seen_shutdown && env != NULL && env->shutdown_timeout != NULL &&
        !cerv_parse_duration(env->shutdown_timeout, &out->shutdown_timeout)) {
        cerv_config_error(error_buf, error_cap, "invalid CERV_SHUTDOWN_TIMEOUT"); return CERV_CONFIG_ERROR;
    }
    if (!seen_immutable && env != NULL && env->immutable != NULL && !cerv_parse_bool(env->immutable, &out->immutable)) {
        cerv_config_error(error_buf, error_cap, "invalid CERV_IMMUTABLE"); return CERV_CONFIG_ERROR;
    }
    if (!seen_spa_fallback && env != NULL && env->spa_fallback != NULL && env->spa_fallback[0] != '\0' &&
        !cerv_parse_spa_fallback(env->spa_fallback, out->spa_fallback, &out->spa_fallback_len)) {
        cerv_config_error(error_buf, error_cap, "invalid CERV_SPA_FALLBACK"); return CERV_CONFIG_ERROR;
    }
    if (out->root_path == NULL && env != NULL && env->root != NULL) {
        if (env->root[0] == '\0') { cerv_config_error(error_buf, error_cap, "invalid CERV_ROOT"); return CERV_CONFIG_ERROR; }
        out->root_path = env->root;
    }

    if (!seen_listen) { cerv_config_error(error_buf, error_cap, "--listen or CERV_LISTEN is required"); return CERV_CONFIG_ERROR; }
    if (out->root_path == NULL) { cerv_config_error(error_buf, error_cap, "ROOT or CERV_ROOT is required"); return CERV_CONFIG_ERROR; }
    if (workers_auto && !cerv_config_detect_auto_workers(&out->workers)) {
        cerv_config_error(error_buf, error_cap, "cannot resolve --workers auto"); return CERV_CONFIG_ERROR;
    }
    if (out->workers == 0U || out->workers > CERV_WORKERS_MAX ||
        (!out->max_connections_auto && out->workers > out->max_connections)) {
        cerv_config_error(error_buf, error_cap, "workers exceed max connections"); return CERV_CONFIG_ERROR;
    }
    return CERV_CONFIG_OK;
}

enum cerv_config_result cerv_config_parse(int argc, char *const argv[], struct cerv_config *out,
                                          char *error_buf, size_t error_cap)
{
    return cerv_config_parse_with_env(argc, argv, NULL, out, error_buf, error_cap);
}
