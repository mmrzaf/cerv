#define _GNU_SOURCE
#include "base/bounds.h"
#include "process/config.h"
#include "process/config_workers.h"
#include "runtime/conn.h"
#include "runtime/capacity.h"
#include "process/diag.h"
#include "runtime/timer.h"
#include "process/supervisor.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>

static unsigned checks;
static unsigned failures;

#define CHECK(expr) do { \
    ++checks; \
    if (!(expr)) { \
        ++failures; \
        fprintf(stderr, "FAIL %s:%d: %s (errno=%d)\n", __FILE__, __LINE__, #expr, errno); \
    } \
} while (0)

static void test_auto_workers(void)
{
    size_t out = 0U;
    CHECK(cerv_config_auto_worker_count(8U, false, 0U, 0U, &out) && out == 8U);
    CHECK(cerv_config_auto_worker_count(8U, true, UINT64_C(200000), UINT64_C(100000), &out) && out == 2U);
    CHECK(cerv_config_auto_worker_count(8U, true, UINT64_C(150000), UINT64_C(100000), &out) && out == 2U);
    CHECK(cerv_config_auto_worker_count(8U, true, UINT64_C(1), UINT64_C(100000), &out) && out == 1U);
    CHECK(!cerv_config_auto_worker_count(0U, false, 0U, 0U, &out));
    CHECK(cerv_config_auto_worker_count(CERV_WORKERS_MAX + 1U, false, 0U, 0U, &out) && out == CERV_WORKERS_MAX);
    CHECK(!cerv_config_auto_worker_count(8U, true, 1U, 0U, &out));
    CHECK(cerv_config_detect_auto_workers(&out));
    CHECK(out >= 1U && out <= CERV_WORKERS_MAX);
}

static void test_cpu_max_parser(void)
{
    bool limited = false;
    uint64_t quota = UINT64_C(0);
    uint64_t period = UINT64_C(0);
    static const unsigned char unlimited[] = "max 100000\n";
    static const unsigned char finite[] = "  150000\t100000\r\n";
    static const unsigned char trailing[] = "200000 100000 junk\n";
    static const unsigned char missing[] = "200000\n";
    static const unsigned char zero[] = "0 100000\n";
    CHECK(cerv_config_parse_cpu_max(unlimited, sizeof(unlimited) - 1U, &limited, &quota, &period));
    CHECK(!limited && quota == UINT64_C(0) && period == UINT64_C(100000));
    CHECK(cerv_config_parse_cpu_max(finite, sizeof(finite) - 1U, &limited, &quota, &period));
    CHECK(limited && quota == UINT64_C(150000) && period == UINT64_C(100000));
    CHECK(!cerv_config_parse_cpu_max(trailing, sizeof(trailing) - 1U, &limited, &quota, &period));
    CHECK(!cerv_config_parse_cpu_max(missing, sizeof(missing) - 1U, &limited, &quota, &period));
    CHECK(!cerv_config_parse_cpu_max(zero, sizeof(zero) - 1U, &limited, &quota, &period));
    CHECK(!cerv_config_parse_cpu_max(unlimited, sizeof(unlimited) - 1U, NULL, &quota, &period));
}

static void test_partitions_and_budgets(void)
{
    size_t out = 0U;
    size_t memory = 0U;
    CHECK(cerv_config_partition_slots(10U, 3U, 0U, &out) && out == 4U);
    CHECK(cerv_config_partition_slots(10U, 3U, 1U, &out) && out == 3U);
    CHECK(cerv_config_partition_slots(10U, 3U, 2U, &out) && out == 3U);
    CHECK(!cerv_config_partition_slots(2U, 3U, 0U, &out));
    CHECK(!cerv_config_partition_slots(10U, 3U, 3U, &out));
    CHECK(cerv_runtime_worker_memory_bytes(1U, &memory));
    CHECK(memory == sizeof(struct cerv_conn) + sizeof(struct cerv_timer_node));
    CHECK(cerv_runtime_required_worker_fds(1U, &out) && out == 2U + CERV_FD_SAFETY_MARGIN);
    CHECK(cerv_runtime_auto_connections(1U, 4096U, UINT64_C(1024), false, &out) && out == 504U);
    CHECK(cerv_runtime_auto_connections(4U, 4096U, UINT64_C(1024), false, &out) && out == 2016U);
    CHECK(cerv_runtime_auto_connections(8U, 4096U, UINT64_C(1024), false, &out) && out == 4032U);
    CHECK(cerv_runtime_auto_connections(8U, 4096U, UINT64_C(16384), false, &out) && out == 4096U);
    CHECK(cerv_runtime_auto_connections(8U, 4096U, UINT64_C(0), true, &out) && out == 4096U);
    CHECK(!cerv_runtime_auto_connections(1U, 4096U, UINT64_C(16), false, &out));
}

struct argv_fixture {
    char storage[20][64];
    char *argv[20];
    int argc;
};

static bool make_args(struct argv_fixture *fixture, const char *const values[], size_t count)
{
    size_t i;
    if (fixture == NULL || values == NULL || count > 20U) return false;
    fixture->argc = (int)count;
    for (i = 0U; i < count; ++i) {
        int n = snprintf(fixture->storage[i], sizeof(fixture->storage[i]), "%s", values[i]);
        if (n < 0 || (size_t)n >= sizeof(fixture->storage[i])) return false;
        fixture->argv[i] = fixture->storage[i];
    }
    return true;
}

static enum cerv_config_result parse_values(const char *const values[], size_t count, struct cerv_config *config)
{
    struct argv_fixture fixture;
    char error[128];
    if (!make_args(&fixture, values, count)) return CERV_CONFIG_ERROR;
    memset(error, 0, sizeof(error));
    return cerv_config_parse(fixture.argc, fixture.argv, config, error, sizeof(error));
}

static void test_cli(void)
{
    struct cerv_config config;
    const char *valid[] = {
        "cerv", "--listen", "127.0.0.1:8080", "--workers", "3", "--max-connections", "10",
        "--header-timeout", "500ms", "--write-timeout=2s", "--max-lifetime", "5m",
        "--shutdown-timeout", "30s", "--immutable", "/srv/www"
    };
    const char *ipv6[] = {"cerv", "--listen=[::1]:8080", "--workers=1", "/tmp"};
    const char *missing_listen[] = {"cerv", "--workers", "1", "/tmp"};
    const char *bad_ipv6[] = {"cerv", "--listen", "::1:8080", "--workers", "1", "/tmp"};
    const char *dup[] = {"cerv", "--listen", "127.0.0.1:1", "--listen", "127.0.0.1:2", "/tmp"};
    const char *unknown[] = {"cerv", "--listen", "127.0.0.1:1", "--wat", "/tmp"};
    const char *zero_duration[] = {"cerv", "--listen", "127.0.0.1:1", "--header-timeout", "0ms", "/tmp"};
    const char *workers_over_slots[] = {"cerv", "--listen", "127.0.0.1:1", "--workers", "3", "--max-connections", "2", "/tmp"};
    const char *mutable[] = {"cerv", "--listen", "127.0.0.1:1", "--workers", "1", "--mutable", "/tmp"};
    const char *help[] = {"cerv", "--help"};
    const char *version[] = {"cerv", "--version"};
    {
        struct argv_fixture fixture;
        char error[128] = {0};
        CHECK(make_args(&fixture, valid, sizeof(valid) / sizeof(valid[0])));
        CHECK(cerv_config_parse(fixture.argc, fixture.argv, &config, error, sizeof(error)) == CERV_CONFIG_OK);
        CHECK(config.workers == 3U && config.max_connections == 10U && config.immutable);
        CHECK(config.listen_addr.ss_family == AF_INET);
        CHECK(config.header_timeout.ns == UINT64_C(500000000));
        CHECK(config.write_timeout.ns == UINT64_C(2000000000));
        CHECK(config.max_lifetime.ns == UINT64_C(300000000000));
        CHECK(config.shutdown_timeout.ns == UINT64_C(30000000000));
        CHECK(config.root_path == fixture.argv[fixture.argc - 1]);
        CHECK(strcmp(config.root_path, "/srv/www") == 0);
    }
    CHECK(parse_values(ipv6, sizeof(ipv6) / sizeof(ipv6[0]), &config) == CERV_CONFIG_OK);
    CHECK(config.listen_addr.ss_family == AF_INET6);
    CHECK(parse_values(missing_listen, sizeof(missing_listen) / sizeof(missing_listen[0]), &config) == CERV_CONFIG_ERROR);
    CHECK(parse_values(bad_ipv6, sizeof(bad_ipv6) / sizeof(bad_ipv6[0]), &config) == CERV_CONFIG_ERROR);
    CHECK(parse_values(dup, sizeof(dup) / sizeof(dup[0]), &config) == CERV_CONFIG_ERROR);
    CHECK(parse_values(unknown, sizeof(unknown) / sizeof(unknown[0]), &config) == CERV_CONFIG_ERROR);
    CHECK(parse_values(zero_duration, sizeof(zero_duration) / sizeof(zero_duration[0]), &config) == CERV_CONFIG_ERROR);
    CHECK(parse_values(workers_over_slots, sizeof(workers_over_slots) / sizeof(workers_over_slots[0]), &config) == CERV_CONFIG_ERROR);
    CHECK(parse_values(mutable, sizeof(mutable) / sizeof(mutable[0]), &config) == CERV_CONFIG_OK && !config.immutable);
    CHECK(parse_values(help, sizeof(help) / sizeof(help[0]), &config) == CERV_CONFIG_HELP);
    CHECK(parse_values(version, sizeof(version) / sizeof(version[0]), &config) == CERV_CONFIG_VERSION);
    CHECK(strstr(cerv_config_help_text(), "--shutdown-timeout") != NULL);
    CHECK(strstr(cerv_version_text(), CERV_VERSION) != NULL);
}

static void test_spa_fallback_cli(void)
{
    struct cerv_config config;
    const char *plain[] = {"cerv", "--listen", "127.0.0.1:1", "--spa-fallback", "/index.html", "/tmp"};
    const char *nested[] = {"cerv", "--listen", "127.0.0.1:1", "--spa-fallback=app/shell.html", "/tmp"};
    const char *well_known[] = {"cerv", "--listen", "127.0.0.1:1", "--spa-fallback", ".well-known/shell.html", "/tmp"};
    const char *dotfile[] = {"cerv", "--listen", "127.0.0.1:1", "--spa-fallback", "/.env", "/tmp"};
    const char *dot_dir[] = {"cerv", "--listen", "127.0.0.1:1", "--spa-fallback", "a/.private/index.html", "/tmp"};
    const char *dot_segment[] = {"cerv", "--listen", "127.0.0.1:1", "--spa-fallback", "a/../index.html", "/tmp"};
    CHECK(parse_values(plain, sizeof(plain) / sizeof(plain[0]), &config) == CERV_CONFIG_OK);
    CHECK(config.spa_fallback_len == strlen("index.html") && memcmp(config.spa_fallback, "index.html", config.spa_fallback_len) == 0);
    CHECK(parse_values(nested, sizeof(nested) / sizeof(nested[0]), &config) == CERV_CONFIG_OK);
    CHECK(config.spa_fallback_len == strlen("app/shell.html"));
    CHECK(parse_values(well_known, sizeof(well_known) / sizeof(well_known[0]), &config) == CERV_CONFIG_OK);
    /* A hidden fallback could never be served, so it is a startup error rather than a per-request 404. */
    CHECK(parse_values(dotfile, sizeof(dotfile) / sizeof(dotfile[0]), &config) == CERV_CONFIG_ERROR);
    CHECK(parse_values(dot_dir, sizeof(dot_dir) / sizeof(dot_dir[0]), &config) == CERV_CONFIG_ERROR);
    CHECK(parse_values(dot_segment, sizeof(dot_segment) / sizeof(dot_segment[0]), &config) == CERV_CONFIG_ERROR);
}

static bool parse_error_contains(const char *const values[], size_t count, const char *needle)
{
    struct argv_fixture fixture;
    struct cerv_config config;
    char error[192] = {0};
    if (!make_args(&fixture, values, count)) return false;
    if (cerv_config_parse(fixture.argc, fixture.argv, &config, error, sizeof(error)) != CERV_CONFIG_ERROR) return false;
    return strstr(error, needle) != NULL;
}

static void test_config_error_messages(void)
{
    const char *bad_listen[] = {"cerv", "--listen", "localhost:80", "/tmp"};
    const char *bad_workers[] = {"cerv", "--listen", "127.0.0.1:1", "--workers", "0", "/tmp"};
    const char *bad_duration[] = {"cerv", "--listen", "127.0.0.1:1", "--header-timeout", "5", "/tmp"};
    const char *bad_spa[] = {"cerv", "--listen", "127.0.0.1:1", "--spa-fallback", "../x", "/tmp"};
    const char *unknown[] = {"cerv", "--listen", "127.0.0.1:1", "--frobnicate", "/tmp"};
    const char *missing_value[] = {"cerv", "/tmp", "--listen"};
    const char *both_cache[] = {"cerv", "--listen", "127.0.0.1:1", "--immutable", "--mutable", "/tmp"};
    const char *no_root[] = {"cerv", "--listen", "127.0.0.1:1"};
    const char *two_roots[] = {"cerv", "--listen", "127.0.0.1:1", "/a", "/b"};
    /* Every rejection names the offending option and says what would have been accepted. */
    CHECK(parse_error_contains(bad_listen, sizeof(bad_listen) / sizeof(bad_listen[0]), "invalid --listen (expected IPV4:PORT or [IPV6]:PORT"));
    CHECK(parse_error_contains(bad_workers, sizeof(bad_workers) / sizeof(bad_workers[0]), "invalid --workers (expected a positive integer up to 1024, or auto)"));
    CHECK(parse_error_contains(bad_duration, sizeof(bad_duration) / sizeof(bad_duration[0]), "ms, s, or m suffix"));
    CHECK(parse_error_contains(bad_spa, sizeof(bad_spa) / sizeof(bad_spa[0]), "invalid --spa-fallback (expected a root-relative file path"));
    CHECK(parse_error_contains(unknown, sizeof(unknown) / sizeof(unknown[0]), "unknown option, or option missing its value: --frobnicate"));
    CHECK(parse_error_contains(missing_value, sizeof(missing_value) / sizeof(missing_value[0]), ": --listen"));
    CHECK(parse_error_contains(both_cache, sizeof(both_cache) / sizeof(both_cache[0]), "--immutable and --mutable cannot be combined"));
    CHECK(parse_error_contains(no_root, sizeof(no_root) / sizeof(no_root[0]), "ROOT or CERV_ROOT is required"));
    CHECK(parse_error_contains(two_roots, sizeof(two_roots) / sizeof(two_roots[0]), "multiple ROOT arguments"));
}

static void test_landlock_option(void)
{
    struct cerv_config config;
    struct cerv_config_env env = {.landlock = "require"};
    struct argv_fixture fixture;
    char error[128] = {0};
    const char *base[] = {"cerv", "--listen", "127.0.0.1:1", "--workers", "1", "/tmp"};
    const char *require[] = {"cerv", "--listen", "127.0.0.1:1", "--workers", "1", "--landlock", "require", "/tmp"};
    const char *require_eq[] = {"cerv", "--listen", "127.0.0.1:1", "--workers", "1", "--landlock=require", "/tmp"};
    const char *automatic[] = {"cerv", "--listen", "127.0.0.1:1", "--workers", "1", "--landlock=auto", "/tmp"};
    const char *bogus[] = {"cerv", "--listen", "127.0.0.1:1", "--workers", "1", "--landlock", "yes", "/tmp"};
    const char *duplicate[] = {"cerv", "--listen", "127.0.0.1:1", "--workers", "1", "--landlock=auto", "--landlock=require", "/tmp"};
    const char *empty[] = {"cerv", "--listen", "127.0.0.1:1", "--workers", "1", "--landlock=", "/tmp"};

    /* Default is auto: Landlock is used when the kernel offers it and never required. */
    CHECK(parse_values(base, sizeof(base) / sizeof(base[0]), &config) == CERV_CONFIG_OK && !config.require_landlock);
    CHECK(parse_values(require, sizeof(require) / sizeof(require[0]), &config) == CERV_CONFIG_OK && config.require_landlock);
    CHECK(parse_values(require_eq, sizeof(require_eq) / sizeof(require_eq[0]), &config) == CERV_CONFIG_OK && config.require_landlock);
    CHECK(parse_values(automatic, sizeof(automatic) / sizeof(automatic[0]), &config) == CERV_CONFIG_OK && !config.require_landlock);
    CHECK(parse_values(bogus, sizeof(bogus) / sizeof(bogus[0]), &config) == CERV_CONFIG_ERROR);
    CHECK(parse_values(duplicate, sizeof(duplicate) / sizeof(duplicate[0]), &config) == CERV_CONFIG_ERROR);
    CHECK(parse_values(empty, sizeof(empty) / sizeof(empty[0]), &config) == CERV_CONFIG_ERROR);

    /* defaults < environment < command line */
    CHECK(make_args(&fixture, base, sizeof(base) / sizeof(base[0])));
    CHECK(cerv_config_parse_with_env(fixture.argc, fixture.argv, &env, &config, error, sizeof(error)) == CERV_CONFIG_OK);
    CHECK(config.require_landlock);
    CHECK(make_args(&fixture, automatic, sizeof(automatic) / sizeof(automatic[0])));
    CHECK(cerv_config_parse_with_env(fixture.argc, fixture.argv, &env, &config, error, sizeof(error)) == CERV_CONFIG_OK);
    CHECK(!config.require_landlock);
    env.landlock = "auto";
    CHECK(make_args(&fixture, require, sizeof(require) / sizeof(require[0])));
    CHECK(cerv_config_parse_with_env(fixture.argc, fixture.argv, &env, &config, error, sizeof(error)) == CERV_CONFIG_OK);
    CHECK(config.require_landlock);
    env.landlock = "maybe";
    CHECK(make_args(&fixture, base, sizeof(base) / sizeof(base[0])));
    CHECK(cerv_config_parse_with_env(fixture.argc, fixture.argv, &env, &config, error, sizeof(error)) == CERV_CONFIG_ERROR);
    CHECK(strstr(error, "CERV_LANDLOCK") != NULL);
    /* An invalid environment value is ignored only when the command line overrides it. */
    CHECK(make_args(&fixture, automatic, sizeof(automatic) / sizeof(automatic[0])));
    CHECK(cerv_config_parse_with_env(fixture.argc, fixture.argv, &env, &config, error, sizeof(error)) == CERV_CONFIG_OK);
    CHECK(strstr(cerv_config_help_text(), "--landlock") != NULL && strstr(cerv_config_help_text(), "CERV_LANDLOCK") != NULL);
}

static void test_environment_config(void)
{
    struct cerv_config config;
    struct cerv_config_env env = {
        .listen = "0.0.0.0:8080",
        .workers = "2",
        .max_connections = "auto",
        .header_timeout = "1s",
        .write_timeout = "2s",
        .max_lifetime = "3m",
        .shutdown_timeout = "4s",
        .root = "/srv/cerv",
        .immutable = "yes",
        .spa_fallback = "/index.html"
    };
    struct argv_fixture fixture;
    char error[128] = {0};
    const char *defaults[] = {"cerv"};
    const char *overrides[] = {"cerv", "--workers", "3", "--max-connections", "12", "/override"};
    const char *help[] = {"cerv", "--help"};
    CHECK(make_args(&fixture, defaults, sizeof(defaults) / sizeof(defaults[0])));
    CHECK(cerv_config_parse_with_env(fixture.argc, fixture.argv, &env, &config, error, sizeof(error)) == CERV_CONFIG_OK);
    CHECK(config.listen_addr.ss_family == AF_INET);
    CHECK(config.workers == 2U && config.max_connections_auto && config.max_connections == CERV_DEFAULT_MAX_CONNECTIONS);
    CHECK(config.root_path == env.root && config.immutable);
    CHECK(config.header_timeout.ns == UINT64_C(1000000000));
    CHECK(config.write_timeout.ns == UINT64_C(2000000000));
    CHECK(config.max_lifetime.ns == UINT64_C(180000000000));
    CHECK(config.shutdown_timeout.ns == UINT64_C(4000000000));
    CHECK(config.spa_fallback_len == strlen("index.html"));
    CHECK(memcmp(config.spa_fallback, "index.html", strlen("index.html")) == 0);

    CHECK(make_args(&fixture, overrides, sizeof(overrides) / sizeof(overrides[0])));
    CHECK(cerv_config_parse_with_env(fixture.argc, fixture.argv, &env, &config, error, sizeof(error)) == CERV_CONFIG_OK);
    CHECK(config.workers == 3U && !config.max_connections_auto && config.max_connections == 12U);
    CHECK(strcmp(config.root_path, "/override") == 0);

    env.immutable = "true";
    {
        const char *mutable_override[] = {"cerv", "--mutable"};
        CHECK(make_args(&fixture, mutable_override, sizeof(mutable_override) / sizeof(mutable_override[0])));
        CHECK(cerv_config_parse_with_env(fixture.argc, fixture.argv, &env, &config, error, sizeof(error)) == CERV_CONFIG_OK);
        CHECK(!config.immutable);
    }

    env.immutable = "maybe";
    CHECK(make_args(&fixture, defaults, sizeof(defaults) / sizeof(defaults[0])));
    CHECK(cerv_config_parse_with_env(fixture.argc, fixture.argv, &env, &config, error, sizeof(error)) == CERV_CONFIG_ERROR);
    CHECK(make_args(&fixture, help, sizeof(help) / sizeof(help[0])));
    CHECK(cerv_config_parse_with_env(fixture.argc, fixture.argv, &env, &config, error, sizeof(error)) == CERV_CONFIG_HELP);
}

static int resource_limit_child(void)
{
    struct cerv_config config;
    struct cerv_resource_plan plan;
    struct rlimit limit = {.rlim_cur = 64U, .rlim_max = 64U};
    memset(&config, 0, sizeof(config));
    config.workers = 1U;
    config.max_connections = 100U;
    if (setrlimit(RLIMIT_NOFILE, &limit) != 0) return 10;
    if (cerv_supervisor_resource_plan(&config, &plan)) return 11;
    config.max_connections = 8U;
    if (!cerv_supervisor_resource_plan(&config, &plan)) return 12;
    if (plan.required_worker_fds != 32U) return 13;
    return 0;
}

static void test_resource_limit(void)
{
    pid_t pid = fork();
    int status = 0;
    CHECK(pid >= (pid_t)0);
    if (pid == (pid_t)0) _exit(resource_limit_child());
    if (pid > (pid_t)0) {
        CHECK(waitpid(pid, &status, 0) == pid);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
}

static void test_diag_drop_when_stderr_full(void)
{
    int pipe_fds[2] = {-1, -1};
    int saved_stderr = -1;
    unsigned char fill[1024];
    struct timespec before;
    struct timespec after;
    uint64_t elapsed_ns = UINT64_C(0);
    memset(fill, 0x5a, sizeof(fill));
    CHECK(pipe2(pipe_fds, O_CLOEXEC | O_NONBLOCK) == 0);
    if (pipe_fds[0] < 0 || pipe_fds[1] < 0) return;
    for (;;) {
        ssize_t n = write(pipe_fds[1], fill, sizeof(fill));
        if (n > 0) continue;
        if (n < 0 && errno == EINTR) continue;
        CHECK(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
        break;
    }
    saved_stderr = dup(STDERR_FILENO);
    CHECK(saved_stderr >= 0);
    if (saved_stderr >= 0) {
        CHECK(dup2(pipe_fds[1], STDERR_FILENO) == STDERR_FILENO);
        cerv_diag_close();
        cerv_diag_prepare();
        CHECK(clock_gettime(CLOCK_MONOTONIC, &before) == 0);
        cerv_diag_message("info", "backpressure_test", "this record must be dropped rather than waiting");
        CHECK(clock_gettime(CLOCK_MONOTONIC, &after) == 0);
        if (after.tv_sec >= before.tv_sec) {
            uint64_t seconds = (uint64_t)(after.tv_sec - before.tv_sec);
            uint64_t before_ns = (uint64_t)before.tv_nsec;
            uint64_t after_ns = (uint64_t)after.tv_nsec;
            if (after_ns >= before_ns) elapsed_ns = seconds * UINT64_C(1000000000) + after_ns - before_ns;
            else if (seconds != UINT64_C(0)) elapsed_ns = (seconds - UINT64_C(1)) * UINT64_C(1000000000) + UINT64_C(1000000000) + after_ns - before_ns;
        }
        cerv_diag_close();
        CHECK(dup2(saved_stderr, STDERR_FILENO) == STDERR_FILENO);
        cerv_diag_prepare();
        CHECK(close(saved_stderr) == 0);
        CHECK(elapsed_ns < UINT64_C(50000000));
    }
    CHECK(close(pipe_fds[0]) == 0);
    CHECK(close(pipe_fds[1]) == 0);
}


static void test_startup_wait_deadline(void)
{
    int signal_pipe[2] = {-1, -1};
    int ready_pipe[2] = {-1, -1};
    pid_t pids[1] = {(pid_t)12345};
    bool alive[1] = {true};
    bool landlock_all = false;
    struct cerv_duration timeout;
    struct timespec before;
    struct timespec after;
    uint64_t elapsed_ns = UINT64_C(0);
    CHECK(pipe2(signal_pipe, O_CLOEXEC | O_NONBLOCK) == 0);
    CHECK(pipe2(ready_pipe, O_CLOEXEC | O_NONBLOCK) == 0);
    CHECK(cerv_duration_from_ms(UINT64_C(20), &timeout));
    CHECK(clock_gettime(CLOCK_MONOTONIC, &before) == 0);
    CHECK(!cerv_supervisor_wait_startup(signal_pipe[0], ready_pipe[0], pids, alive, 1U, timeout, &landlock_all));
    CHECK(clock_gettime(CLOCK_MONOTONIC, &after) == 0);
    if (after.tv_sec >= before.tv_sec) {
        uint64_t seconds = (uint64_t)(after.tv_sec - before.tv_sec);
        uint64_t before_ns = (uint64_t)before.tv_nsec;
        uint64_t after_ns = (uint64_t)after.tv_nsec;
        if (after_ns >= before_ns) elapsed_ns = seconds * UINT64_C(1000000000) + after_ns - before_ns;
        else if (seconds != UINT64_C(0)) elapsed_ns = (seconds - UINT64_C(1)) * UINT64_C(1000000000) + UINT64_C(1000000000) + after_ns - before_ns;
    }
    CHECK(elapsed_ns >= UINT64_C(10000000) && elapsed_ns < UINT64_C(500000000));
    CHECK(close(signal_pipe[0]) == 0);
    CHECK(close(signal_pipe[1]) == 0);
    CHECK(close(ready_pipe[0]) == 0);
    CHECK(close(ready_pipe[1]) == 0);
}

int main(void)
{
    test_auto_workers();
    test_cpu_max_parser();
    test_partitions_and_budgets();
    test_cli();
    test_spa_fallback_cli();
    test_landlock_option();
    test_config_error_messages();
    test_environment_config();
    test_resource_limit();
    test_diag_drop_when_stderr_full();
    test_startup_wait_deadline();
    if (failures != 0U) {
        fprintf(stderr, "supervisor unit: %u/%u checks failed\n", failures, checks);
        return 1;
    }
    printf("supervisor unit: %u checks passed\n", checks);
    return 0;
}
