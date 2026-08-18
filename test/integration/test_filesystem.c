#define _GNU_SOURCE 1
#include "linux/fs.h"
#include "http/http_request.h"
#include "http/http_target.h"
#include "serve/media_type.h"
#include "serve/representation.h"
#include "serve/response.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static unsigned checks = 0U;
static unsigned failures = 0U;

#define CHECK(expr) do { \
    ++checks; \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++failures; \
    } \
} while (0)

static bool path_join(char *out, size_t cap, const char *a, const char *b)
{
    int n = snprintf(out, cap, "%s/%s", a, b);
    return n >= 0 && (size_t)n < cap;
}

static bool write_bytes(const char *path, const char *bytes)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    size_t len = strlen(bytes);
    ssize_t wrote;
    if (fd < 0) return false;
    wrote = write(fd, bytes, len);
    if (close(fd) != 0) return false;
    return wrote >= 0 && (size_t)wrote == len;
}

static struct cerv_path logical_path(const char *text)
{
    struct cerv_path path = {0};
    size_t len = strlen(text);
    CHECK(len < sizeof(path.bytes));
    if (len < sizeof(path.bytes)) {
        memcpy(path.bytes, text, len + 1U);
        path.len = len;
    }
    return path;
}

static struct cerv_accept_encoding accept_encoding(const char *text)
{
    struct cerv_accept_encoding ae;
    cerv_accept_encoding_init(&ae);
    if (text != NULL) CHECK(cerv_accept_encoding_add_field(&ae, (struct cerv_span){
        .ptr = (const unsigned char *)text, .len = strlen(text)}));
    return ae;
}

static enum cerv_parse_result parse_request(const char *wire, struct cerv_http_request *out)
{
    return cerv_http_request_parse((const unsigned char *)wire, strlen(wire), out);
}

static void test_fs_and_representation(const char *root_dir, const char *outside_dir)
{
    struct cerv_fs_root root = {.fd = -1};
    struct cerv_path app = logical_path("assets/app.js");
    struct cerv_accept_encoding ae;
    struct cerv_representation rep;
    char path[1024];
    char outside_file[1024];
    char symlink_path[1024];
    char intermediate[1024];
    char fifo_path[1024];
    char dir_path[1024];
    char hard_path[1024];
    char zero_path[1024];
    char sparse_path[1024];
    char unix_path[1024];
    char loop_a[1024];
    char loop_b[1024];
    char device_path[1024];
    char denied_file[1024];
    char denied_dir[1024];
    char denied_child[1024];
    int sock = -1;
    struct sockaddr_un addr;

    CHECK(chmod(root_dir, 0755) == 0);
    CHECK(cerv_fs_root_open(NULL, &root) == CERV_FS_ROOT_INVALID);
    CHECK(cerv_fs_root_open("", &root) == CERV_FS_ROOT_INVALID);
    CHECK(cerv_fs_root_open("/definitely/not/a/cerv/root", &root) == CERV_FS_ROOT_INVALID);
    CHECK(cerv_fs_root_open(root_dir, &root) == CERV_FS_ROOT_OK);
    CHECK(root.fd >= 0);
    CHECK(cerv_fs_classify_errno(ENOENT) == CERV_FS_NOT_FOUND);
    CHECK(cerv_fs_classify_errno(ELOOP) == CERV_FS_POLICY_REJECTED);
    CHECK(cerv_fs_classify_errno(EACCES) == CERV_FS_FORBIDDEN);
    CHECK(cerv_fs_classify_errno(EMFILE) == CERV_FS_FD_EXHAUSTED_PROCESS);
    CHECK(cerv_fs_classify_errno(ENFILE) == CERV_FS_FD_EXHAUSTED_SYSTEM);
    CHECK(cerv_fs_classify_errno(EIO) == CERV_FS_IO);
    CHECK(cerv_fs_classify_errno(ENOSYS) == CERV_FS_UNSUPPORTED);

    CHECK(path_join(path, sizeof(path), root_dir, "assets"));
    CHECK(mkdir(path, 0755) == 0);
    CHECK(path_join(path, sizeof(path), root_dir, "assets/app.js"));
    CHECK(write_bytes(path, "IDENTITY"));
    CHECK(path_join(path, sizeof(path), root_dir, "assets/app.js.br"));
    CHECK(write_bytes(path, "BR"));
    CHECK(path_join(path, sizeof(path), root_dir, "assets/app.js.gz"));
    CHECK(write_bytes(path, "GZIP"));
    {
        struct cerv_fs_root not_dir = {.fd = -1};
        CHECK(path_join(path, sizeof(path), root_dir, "assets/app.js"));
        CHECK(cerv_fs_root_open(path, &not_dir) == CERV_FS_ROOT_INVALID);
        CHECK(not_dir.fd == -1);
    }

    ae = accept_encoding(NULL);
    CHECK(cerv_representation_select(&root, &app, &ae, &rep) == CERV_REPRESENTATION_OK);
    CHECK(rep.encoding == CERV_ENCODING_BR);
    CHECK(rep.file.size == UINT64_C(2));
    CHECK(strcmp(rep.media_type, "text/javascript; charset=utf-8") == 0);
    CHECK(rep.etag_len == 122U && rep.etag[0] == (unsigned char)'W');
    {
        struct cerv_entity_tag parsed_tag;
        CHECK(cerv_entity_tag_parse(cerv_representation_etag(&rep), &parsed_tag) == CERV_ETAG_OK);
        CHECK(parsed_tag.weak);
    }
    cerv_representation_close(&rep);

    ae = accept_encoding("gzip;q=1, br;q=0.5, identity;q=0.2");
    CHECK(cerv_representation_select(&root, &app, &ae, &rep) == CERV_REPRESENTATION_OK);
    CHECK(rep.encoding == CERV_ENCODING_GZIP && rep.file.size == UINT64_C(4));
    cerv_representation_close(&rep);

    ae = accept_encoding("");
    CHECK(cerv_representation_select(&root, &app, &ae, &rep) == CERV_REPRESENTATION_OK);
    CHECK(rep.encoding == CERV_ENCODING_IDENTITY && rep.file.size == UINT64_C(8));
    cerv_representation_close(&rep);

    ae = accept_encoding("identity;q=1, br;q=0.9, gzip;q=0.8");
    CHECK(cerv_representation_select(&root, &app, &ae, &rep) == CERV_REPRESENTATION_OK);
    CHECK(rep.encoding == CERV_ENCODING_IDENTITY);
    cerv_representation_close(&rep);

    {
        unsigned char identity_etag[CERV_ETAG_WIRE_MAX];
        unsigned char gzip_etag[CERV_ETAG_WIRE_MAX];
        size_t identity_len;
        size_t gzip_len;
        ae = accept_encoding("identity");
        CHECK(cerv_representation_select(&root, &app, &ae, &rep) == CERV_REPRESENTATION_OK);
        identity_len = rep.etag_len;
        memcpy(identity_etag, rep.etag, identity_len);
        cerv_representation_close(&rep);
        ae = accept_encoding("gzip");
        CHECK(cerv_representation_select(&root, &app, &ae, &rep) == CERV_REPRESENTATION_OK);
        gzip_len = rep.etag_len;
        memcpy(gzip_etag, rep.etag, gzip_len);
        CHECK(identity_len != gzip_len || memcmp(identity_etag, gzip_etag, identity_len) != 0);
        cerv_representation_close(&rep);
    }

    ae = accept_encoding("br;q=0, gzip;q=0, identity;q=0");
    CHECK(cerv_representation_select(&root, &app, &ae, &rep) == CERV_REPRESENTATION_NOT_ACCEPTABLE);

    CHECK(path_join(path, sizeof(path), root_dir, "only.js.br"));
    CHECK(write_bytes(path, "ONLYBR"));
    {
        struct cerv_path only = logical_path("only.js");
        ae = accept_encoding("br");
        CHECK(cerv_representation_select(&root, &only, &ae, &rep) == CERV_REPRESENTATION_OK);
        CHECK(rep.encoding == CERV_ENCODING_BR);
        cerv_representation_close(&rep);
    }

    CHECK(path_join(path, sizeof(path), root_dir, "fallback.js"));
    CHECK(write_bytes(path, "FALLBACK"));
    CHECK(path_join(outside_file, sizeof(outside_file), outside_dir, "secret"));
    CHECK(write_bytes(outside_file, "LEAK"));
    CHECK(path_join(symlink_path, sizeof(symlink_path), root_dir, "fallback.js.br"));
    CHECK(symlink(outside_file, symlink_path) == 0);
    {
        struct cerv_path fallback = logical_path("fallback.js");
        ae = accept_encoding("br;q=1, identity;q=0.5");
        CHECK(cerv_representation_select(&root, &fallback, &ae, &rep) == CERV_REPRESENTATION_OK);
        CHECK(rep.encoding == CERV_ENCODING_IDENTITY);
        cerv_representation_close(&rep);
    }

    /* A forbidden optional sidecar can fall back; an entirely forbidden resource remains 403. */
    CHECK(path_join(path, sizeof(path), root_dir, "private.js"));
    CHECK(write_bytes(path, "PUBLIC"));
    CHECK(path_join(symlink_path, sizeof(symlink_path), root_dir, "private.js.br"));
    CHECK(write_bytes(symlink_path, "PRIVATE"));
    CHECK(chmod(symlink_path, 0000) == 0);
    CHECK(path_join(path, sizeof(path), root_dir, "secret.js"));
    CHECK(write_bytes(path, "SECRET"));
    CHECK(chmod(path, 0000) == 0);
    {
        pid_t child = fork();
        CHECK(child >= 0);
        if (child == 0) {
            struct cerv_path private_path = logical_path("private.js");
            struct cerv_path secret_path = logical_path("secret.js");
            struct cerv_accept_encoding child_ae = accept_encoding("br;q=1, identity;q=0.5");
            struct cerv_representation child_rep;
            if (geteuid() == 0 && (setgid((gid_t)65534) != 0 || setuid((uid_t)65534) != 0)) _exit(2);
            if (cerv_representation_select(&root, &private_path, &child_ae, &child_rep) != CERV_REPRESENTATION_OK) _exit(3);
            if (child_rep.encoding != CERV_ENCODING_IDENTITY) _exit(4);
            cerv_representation_close(&child_rep);
            child_ae = accept_encoding("");
            if (cerv_representation_select(&root, &secret_path, &child_ae, &child_rep) != CERV_REPRESENTATION_FORBIDDEN) _exit(5);
            _exit(0);
        } else if (child > 0) {
            int status = 0;
            CHECK(waitpid(child, &status, 0) == child);
            CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }

    /* The direct FS API rejects malformed/over-domain path strings before syscall traversal. */
    {
        char overlong[CERV_REPRESENTATION_PATH_BYTES_MAX + 2U];
        struct cerv_fs_file file = {.fd = -1};
        memset(overlong, 'a', sizeof(overlong));
        overlong[sizeof(overlong) - 1U] = '\0';
        CHECK(cerv_fs_open_regular(&root, overlong, &file) == CERV_FS_POLICY_REJECTED);
        CHECK(cerv_fs_open_regular(&root, "/etc/passwd", &file) == CERV_FS_POLICY_REJECTED);
        CHECK(cerv_fs_open_regular(&root, "../etc/passwd", &file) == CERV_FS_POLICY_REJECTED);
    }

    CHECK(path_join(outside_file, sizeof(outside_file), outside_dir, "secret"));
    CHECK(path_join(symlink_path, sizeof(symlink_path), root_dir, "link"));
    CHECK(symlink(outside_file, symlink_path) == 0);
    {
        struct cerv_fs_file file = {.fd = -1};
        CHECK(cerv_fs_open_regular(&root, "link", &file) == CERV_FS_POLICY_REJECTED);
        CHECK(file.fd == -1);
    }

    CHECK(path_join(intermediate, sizeof(intermediate), root_dir, "escape"));
    CHECK(symlink(outside_dir, intermediate) == 0);
    {
        struct cerv_fs_file file = {.fd = -1};
        CHECK(cerv_fs_open_regular(&root, "escape/secret", &file) == CERV_FS_POLICY_REJECTED);
    }

    CHECK(path_join(loop_a, sizeof(loop_a), root_dir, "loop-a"));
    CHECK(path_join(loop_b, sizeof(loop_b), root_dir, "loop-b"));
    CHECK(symlink("loop-b", loop_a) == 0);
    CHECK(symlink("loop-a", loop_b) == 0);
    {
        struct cerv_fs_file file = {.fd = -1};
        CHECK(cerv_fs_open_regular(&root, "loop-a", &file) == CERV_FS_POLICY_REJECTED);
    }

    CHECK(path_join(fifo_path, sizeof(fifo_path), root_dir, "pipe"));
    CHECK(mkfifo(fifo_path, 0644) == 0);
    {
        struct cerv_fs_file file = {.fd = -1};
        CHECK(cerv_fs_open_regular(&root, "pipe", &file) == CERV_FS_NOT_REGULAR);
    }

    CHECK(path_join(dir_path, sizeof(dir_path), root_dir, "directory"));
    CHECK(mkdir(dir_path, 0755) == 0);
    {
        struct cerv_fs_file file = {.fd = -1};
        CHECK(cerv_fs_open_regular(&root, "directory", &file) == CERV_FS_NOT_REGULAR);
    }

    CHECK(path_join(hard_path, sizeof(hard_path), root_dir, "hard"));
    CHECK(link(outside_file, hard_path) == 0);
    {
        struct cerv_fs_file file = {.fd = -1};
        unsigned char data[4];
        CHECK(cerv_fs_open_regular(&root, "hard", &file) == CERV_FS_OK);
        CHECK(pread(file.fd, data, sizeof(data), (off_t)0) == (ssize_t)sizeof(data));
        CHECK(memcmp(data, "LEAK", sizeof(data)) == 0);
        cerv_fs_file_close(&file);
    }

    CHECK(path_join(zero_path, sizeof(zero_path), root_dir, "zero"));
    CHECK(write_bytes(zero_path, ""));
    {
        struct cerv_fs_file file = {.fd = -1};
        CHECK(cerv_fs_open_regular(&root, "zero", &file) == CERV_FS_OK && file.size == UINT64_C(0));
        cerv_fs_file_close(&file);
    }

    CHECK(path_join(sparse_path, sizeof(sparse_path), root_dir, "sparse"));
    {
        int fd = open(sparse_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        CHECK(fd >= 0);
        if (fd >= 0) {
            CHECK(ftruncate(fd, (off_t)1073741824) == 0);
            CHECK(close(fd) == 0);
        }
    }
    {
        struct cerv_fs_file file = {.fd = -1};
        CHECK(cerv_fs_open_regular(&root, "sparse", &file) == CERV_FS_OK && file.size == UINT64_C(1073741824));
        cerv_fs_file_close(&file);
    }

    CHECK(path_join(unix_path, sizeof(unix_path), root_dir, "sock"));
    sock = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    CHECK(sock >= 0);
    if (sock >= 0) {
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        CHECK(strlen(unix_path) < sizeof(addr.sun_path));
        if (strlen(unix_path) < sizeof(addr.sun_path)) {
            memcpy(addr.sun_path, unix_path, strlen(unix_path) + 1U);
            CHECK(bind(sock, (const struct sockaddr *)&addr, sizeof(addr)) == 0);
            {
                struct cerv_fs_file file = {.fd = -1};
                enum cerv_fs_result result = cerv_fs_open_regular(&root, "sock", &file);
                CHECK(result == CERV_FS_NOT_REGULAR || result == CERV_FS_NOT_FOUND);
            }
        }
        CHECK(close(sock) == 0);
    }

    CHECK(path_join(device_path, sizeof(device_path), root_dir, "device"));
    if (mknod(device_path, S_IFCHR | 0600, makedev(1U, 3U)) == 0) {
        struct cerv_fs_file file = {.fd = -1};
        CHECK(cerv_fs_open_regular(&root, "device", &file) == CERV_FS_NOT_REGULAR);
    } else {
        CHECK(errno == EPERM || errno == EACCES || errno == EINVAL);
    }

    /* Permission-denied file and directory components remain a distinct filesystem class. */
    CHECK(path_join(denied_file, sizeof(denied_file), root_dir, "denied-file"));
    CHECK(write_bytes(denied_file, "NO"));
    CHECK(chmod(denied_file, 0000) == 0);
    CHECK(path_join(denied_dir, sizeof(denied_dir), root_dir, "denied-dir"));
    CHECK(mkdir(denied_dir, 0700) == 0);
    CHECK(path_join(denied_child, sizeof(denied_child), denied_dir, "child"));
    CHECK(write_bytes(denied_child, "NO"));
    CHECK(chmod(denied_dir, 0000) == 0);
    {
        pid_t child = fork();
        CHECK(child >= 0);
        if (child == 0) {
            struct cerv_fs_file file = {.fd = -1};
            if (geteuid() == 0 && (setgid((gid_t)65534) != 0 || setuid((uid_t)65534) != 0)) _exit(2);
            if (cerv_fs_open_regular(&root, "denied-file", &file) != CERV_FS_FORBIDDEN) _exit(3);
            if (cerv_fs_open_regular(&root, "denied-dir/child", &file) != CERV_FS_FORBIDDEN) _exit(4);
            _exit(0);
        } else if (child > 0) {
            int status = 0;
            CHECK(waitpid(child, &status, 0) == child);
            CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }

    /* Open-FD race boundary: replacement/unlink cannot retarget an already opened representation. */
    CHECK(path_join(path, sizeof(path), root_dir, "racefile"));
    CHECK(write_bytes(path, "OLD"));
    {
        struct cerv_fs_file old_file = {.fd = -1};
        char replacement[1024];
        unsigned char bytes[3];
        CHECK(cerv_fs_open_regular(&root, "racefile", &old_file) == CERV_FS_OK);
        CHECK(path_join(replacement, sizeof(replacement), root_dir, "replacement"));
        CHECK(write_bytes(replacement, "NEW"));
        CHECK(rename(replacement, path) == 0);
        CHECK(pread(old_file.fd, bytes, sizeof(bytes), (off_t)0) == (ssize_t)sizeof(bytes));
        CHECK(memcmp(bytes, "OLD", sizeof(bytes)) == 0);
        cerv_fs_file_close(&old_file);
        {
            struct cerv_fs_file new_file = {.fd = -1};
            CHECK(cerv_fs_open_regular(&root, "racefile", &new_file) == CERV_FS_OK);
            CHECK(pread(new_file.fd, bytes, sizeof(bytes), (off_t)0) == (ssize_t)sizeof(bytes));
            CHECK(memcmp(bytes, "NEW", sizeof(bytes)) == 0);
            CHECK(unlink(path) == 0);
            CHECK(pread(new_file.fd, bytes, sizeof(bytes), (off_t)0) == (ssize_t)sizeof(bytes));
            CHECK(memcmp(bytes, "NEW", sizeof(bytes)) == 0);
            cerv_fs_file_close(&new_file);
        }
    }

    /* Concurrent directory/symlink mutation: successful opens must never disclose the outside fixture. */
    CHECK(path_join(path, sizeof(path), root_dir, "slot"));
    CHECK(mkdir(path, 0755) == 0);
    {
        char safe_file[1024];
        pid_t child;
        CHECK(path_join(safe_file, sizeof(safe_file), path, "file"));
        CHECK(write_bytes(safe_file, "SAFE"));
        child = fork();
        CHECK(child >= 0);
        if (child == 0) {
            char slot[1024];
            char real[1024];
            unsigned i;
            if (!path_join(slot, sizeof(slot), root_dir, "slot") || !path_join(real, sizeof(real), root_dir, "slot.real")) _exit(2);
            for (i = 0U; i < 3000U; ++i) {
                if (rename(slot, real) == 0) {
                    (void)symlink(outside_dir, slot);
                    (void)unlink(slot);
                    (void)rename(real, slot);
                }
            }
            _exit(0);
        } else if (child > 0) {
            unsigned i;
            bool race_safe = true;
            for (i = 0U; i < 6000U; ++i) {
                struct cerv_fs_file file = {.fd = -1};
                enum cerv_fs_result result = cerv_fs_open_regular(&root, "slot/file", &file);
                if (result == CERV_FS_OK) {
                    unsigned char bytes[4];
                    if (pread(file.fd, bytes, sizeof(bytes), (off_t)0) != (ssize_t)sizeof(bytes) ||
                        memcmp(bytes, "SAFE", sizeof(bytes)) != 0) race_safe = false;
                    cerv_fs_file_close(&file);
                } else if (result != CERV_FS_NOT_FOUND && result != CERV_FS_POLICY_REJECTED) {
                    race_safe = false;
                }
            }
            CHECK(race_safe);
            {
                int status = 0;
                CHECK(waitpid(child, &status, 0) == child);
                CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
            }
        }
    }

    /* Runtime per-process FD exhaustion is operational, never flattened to 404. */
    {
        pid_t child = fork();
        CHECK(child >= 0);
        if (child == 0) {
            struct rlimit limit = {.rlim_cur = 16U, .rlim_max = 16U};
            int fds[32];
            size_t used = 0U;
            struct cerv_fs_file file = {.fd = -1};
            if (setrlimit(RLIMIT_NOFILE, &limit) != 0) _exit(2);
            while (used < sizeof(fds) / sizeof(fds[0])) {
                int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
                if (fd < 0) break;
                fds[used++] = fd;
            }
            if (cerv_fs_open_regular(&root, "assets/app.js", &file) != CERV_FS_FD_EXHAUSTED_PROCESS) _exit(3);
            _exit(0);
        } else if (child > 0) {
            int status = 0;
            CHECK(waitpid(child, &status, 0) == child);
            CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }

    cerv_fs_root_close(&root);
    CHECK(root.fd == -1);
}


static void test_linux_mount_and_magic_link_policy(void)
{
    struct cerv_fs_root root = {.fd = -1};
    struct cerv_fs_file file = {.fd = -1};
    char magic_path[128];
    int n;

    CHECK(cerv_fs_root_open("/", &root) == CERV_FS_ROOT_OK);
    CHECK(cerv_fs_open_regular(&root, "proc/version", &file) == CERV_FS_OK);
    cerv_fs_file_close(&file);

    n = snprintf(magic_path, sizeof(magic_path), "proc/%ld/fd/%d", (long)getpid(), root.fd);
    CHECK(n > 0 && (size_t)n < sizeof(magic_path));
    if (n > 0 && (size_t)n < sizeof(magic_path)) {
        CHECK(cerv_fs_open_regular(&root, magic_path, &file) == CERV_FS_POLICY_REJECTED);
    }
    cerv_fs_root_close(&root);
    cerv_fs_root_close(&root);
    CHECK(root.fd == -1);
}

static struct cerv_representation synthetic_rep(enum cerv_content_encoding encoding, uint64_t size, int64_t mtime)
{
    struct cerv_representation rep = {0};
    static const unsigned char tag[] = "W/\"abc\"";
    rep.file.fd = 7;
    rep.file.size = size;
    rep.file.mtime_sec = mtime;
    rep.encoding = encoding;
    rep.media_type = "text/javascript; charset=utf-8";
    memcpy(rep.etag, tag, sizeof(tag) - 1U);
    rep.etag_len = sizeof(tag) - 1U;
    return rep;
}

static bool headers_equal(const struct cerv_response_plan *plan, const char *expected)
{
    size_t len = strlen(expected);
    return plan->header_len == len && memcmp(plan->headers, expected, len) == 0;
}

static bool headers_contain(const struct cerv_response_plan *plan, const char *needle)
{
    return memmem(plan->headers, plan->header_len, needle, strlen(needle)) != NULL;
}

static void test_response_semantics(void)
{
    static const int64_t now = INT64_C(784111777); /* Sun, 06 Nov 1994 08:49:37 GMT */
    struct cerv_http_request req;
    struct cerv_representation rep = synthetic_rep(CERV_ENCODING_IDENTITY, UINT64_C(10), now);
    struct cerv_response_plan plan;

    CHECK(parse_request("GET /app.js HTTP/1.1\r\nHost: example\r\n\r\n", &req) == CERV_PARSE_OK);
    CHECK(cerv_response_plan_resource(&req, CERV_REPRESENTATION_OK, &rep, now, false, &plan));
    CHECK(plan.status == CERV_STATUS_200 && plan.send_file && plan.file_offset == UINT64_C(0) && plan.file_count == UINT64_C(10));
    CHECK(headers_equal(&plan,
        "HTTP/1.1 200 OK\r\n"
        "Date: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
        "Connection: close\r\n"
        "Content-Type: text/javascript; charset=utf-8\r\n"
        "Vary: Accept-Encoding\r\n"
        "ETag: W/\"abc\"\r\n"
        "Last-Modified: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
        "Cache-Control: no-cache\r\n"
        "Accept-Ranges: bytes\r\n"
        "Content-Length: 10\r\n"
        "X-Content-Type-Options: nosniff\r\n\r\n"));

    CHECK(parse_request("HEAD /app.js HTTP/1.1\r\nHost: example\r\nRange: bytes=2-5\r\n\r\n", &req) == CERV_PARSE_OK);
    CHECK(cerv_response_plan_resource(&req, CERV_REPRESENTATION_OK, &rep, now, false, &plan));
    CHECK(plan.status == CERV_STATUS_200 && !plan.send_file && !plan.send_body && plan.file_count == UINT64_C(10));

    CHECK(parse_request("GET /app.js HTTP/1.1\r\nHost: example\r\nIf-None-Match: \"abc\"\r\n\r\n", &req) == CERV_PARSE_OK);
    CHECK(cerv_response_plan_resource(&req, CERV_REPRESENTATION_OK, &rep, now, false, &plan));
    CHECK(plan.status == CERV_STATUS_304 && !plan.send_file && !plan.send_body);
    CHECK(headers_equal(&plan,
        "HTTP/1.1 304 Not Modified\r\n"
        "Date: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
        "Connection: close\r\n"
        "Vary: Accept-Encoding\r\n"
        "ETag: W/\"abc\"\r\n"
        "Last-Modified: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
        "Cache-Control: no-cache\r\n\r\n"));
    CHECK(!headers_contain(&plan, "Content-Length"));

    CHECK(parse_request("GET /app.js HTTP/1.1\r\nHost: example\r\nIf-Modified-Since: Sun, 06 Nov 1994 08:49:37 GMT\r\n\r\n", &req) == CERV_PARSE_OK);
    CHECK(cerv_response_plan_resource(&req, CERV_REPRESENTATION_OK, &rep, now, false, &plan));
    CHECK(plan.status == CERV_STATUS_304);

    CHECK(parse_request("GET /app.js HTTP/1.1\r\nHost: example\r\nIf-None-Match: *\r\n\r\n", &req) == CERV_PARSE_OK);
    CHECK(cerv_response_plan_resource(&req, CERV_REPRESENTATION_OK, &rep, now, false, &plan));
    CHECK(plan.status == CERV_STATUS_304);

    CHECK(parse_request("GET /app.js HTTP/1.1\r\nHost: example\r\nIf-Modified-Since: invalid\r\n\r\n", &req) == CERV_PARSE_OK);
    CHECK(cerv_response_plan_resource(&req, CERV_REPRESENTATION_OK, &rep, now, false, &plan));
    CHECK(plan.status == CERV_STATUS_200);

    CHECK(parse_request("GET /app.js HTTP/1.1\r\nHost: example\r\nIf-Modified-Since: Sun, 06 Nov 1994 08:49:37 GMT\r\nIf-Modified-Since: Sun, 06 Nov 1994 08:49:37 GMT\r\n\r\n", &req) == CERV_PARSE_OK);
    CHECK(cerv_response_plan_resource(&req, CERV_REPRESENTATION_OK, &rep, now, false, &plan));
    CHECK(plan.status == CERV_STATUS_200);

    CHECK(parse_request("GET /app.js HTTP/1.1\r\nHost: example\r\nIf-None-Match: \"no\"\r\nIf-Modified-Since: Sun, 06 Nov 1994 08:49:37 GMT\r\n\r\n", &req) == CERV_PARSE_OK);
    CHECK(cerv_response_plan_resource(&req, CERV_REPRESENTATION_OK, &rep, now, false, &plan));
    CHECK(plan.status == CERV_STATUS_200);

    CHECK(parse_request("GET /app.js HTTP/1.1\r\nHost: example\r\nRange: bytes=2-5\r\n\r\n", &req) == CERV_PARSE_OK);
    CHECK(cerv_response_plan_resource(&req, CERV_REPRESENTATION_OK, &rep, now, false, &plan));
    CHECK(plan.status == CERV_STATUS_206 && plan.send_file && plan.file_offset == (off_t)2 && plan.file_count == UINT64_C(4));
    CHECK(headers_equal(&plan,
        "HTTP/1.1 206 Partial Content\r\n"
        "Date: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
        "Connection: close\r\n"
        "Content-Type: text/javascript; charset=utf-8\r\n"
        "Vary: Accept-Encoding\r\n"
        "ETag: W/\"abc\"\r\n"
        "Last-Modified: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
        "Cache-Control: no-cache\r\n"
        "Accept-Ranges: bytes\r\n"
        "Content-Range: bytes 2-5/10\r\n"
        "Content-Length: 4\r\n"
        "X-Content-Type-Options: nosniff\r\n\r\n"));

    CHECK(parse_request("GET /app.js HTTP/1.1\r\nHost: example\r\nRange: bytes=10-\r\n\r\n", &req) == CERV_PARSE_OK);
    CHECK(cerv_response_plan_resource(&req, CERV_REPRESENTATION_OK, &rep, now, false, &plan));
    CHECK(plan.status == CERV_STATUS_416 && !plan.send_file && plan.send_body);
    CHECK(headers_equal(&plan,
        "HTTP/1.1 416 Range Not Satisfiable\r\n"
        "Date: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
        "Connection: close\r\n"
        "Content-Range: bytes */10\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Length: 26\r\n"
        "X-Content-Type-Options: nosniff\r\n\r\n"));

    CHECK(parse_request("GET /app.js HTTP/1.1\r\nHost: example\r\nRange: bytes=0-0,2-2\r\n\r\n", &req) == CERV_PARSE_OK);
    CHECK(cerv_response_plan_resource(&req, CERV_REPRESENTATION_OK, &rep, now, false, &plan));
    CHECK(plan.status == CERV_STATUS_200);

    CHECK(parse_request("GET /app.js HTTP/1.1\r\nHost: example\r\nRange: bytes=2-5\r\nIf-Range: W/\"abc\"\r\n\r\n", &req) == CERV_PARSE_OK);
    CHECK(cerv_response_plan_resource(&req, CERV_REPRESENTATION_OK, &rep, now, false, &plan));
    CHECK(plan.status == CERV_STATUS_200);

    CHECK(parse_request("GET /app.js HTTP/1.1\r\nHost: example\r\nRange: bytes=2-5\r\nIf-Range: \"abc\"\r\n\r\n", &req) == CERV_PARSE_OK);
    CHECK(cerv_response_plan_resource(&req, CERV_REPRESENTATION_OK, &rep, now, false, &plan));
    CHECK(plan.status == CERV_STATUS_200);

    CHECK(parse_request("GET /app.js HTTP/1.1\r\nHost: example\r\nRange: bytes=2-5\r\nIf-Range: Sun, 06 Nov 1994 08:49:37 GMT\r\n\r\n", &req) == CERV_PARSE_OK);
    CHECK(cerv_response_plan_resource(&req, CERV_REPRESENTATION_OK, &rep, now, false, &plan));
    CHECK(plan.status == CERV_STATUS_200);

    rep = synthetic_rep(CERV_ENCODING_GZIP, UINT64_C(4), now + INT64_C(100));
    CHECK(parse_request("GET /app.js HTTP/1.1\r\nHost: example\r\n\r\n", &req) == CERV_PARSE_OK);
    CHECK(cerv_response_plan_resource(&req, CERV_REPRESENTATION_OK, &rep, now, true, &plan));
    CHECK(plan.status == CERV_STATUS_200 && plan.file_count == UINT64_C(4));
    CHECK(headers_contain(&plan, "Content-Encoding: gzip\r\n"));
    CHECK(headers_contain(&plan, "Cache-Control: public, max-age=31536000, immutable\r\n"));
    CHECK(headers_contain(&plan, "Last-Modified: Sun, 06 Nov 1994 08:49:37 GMT\r\n"));

    CHECK(parse_request("GET /app.js HTTP/1.1\r\nHost: example\r\nIf-None-Match: \"abc\"\r\n\r\n", &req) == CERV_PARSE_OK);
    CHECK(cerv_response_plan_resource(&req, CERV_REPRESENTATION_OK, &rep, now, true, &plan));
    CHECK(plan.status == CERV_STATUS_304 && headers_contain(&plan, "Content-Encoding: gzip\r\n"));
    CHECK(headers_contain(&plan, "Vary: Accept-Encoding\r\n"));
    CHECK(!headers_contain(&plan, "Content-Length:"));

    rep = synthetic_rep(CERV_ENCODING_IDENTITY, UINT64_C(0), now);
    CHECK(parse_request("GET /empty HTTP/1.1\r\nHost: example\r\nRange: bytes=0-\r\n\r\n", &req) == CERV_PARSE_OK);
    CHECK(cerv_response_plan_resource(&req, CERV_REPRESENTATION_OK, &rep, now, false, &plan));
    CHECK(plan.status == CERV_STATUS_416 && headers_contain(&plan, "Content-Range: bytes */0\r\n"));

    CHECK(parse_request("GET /app.js HTTP/1.0\r\n\r\n", &req) == CERV_PARSE_OK);
    rep = synthetic_rep(CERV_ENCODING_IDENTITY, UINT64_C(10), now);
    CHECK(cerv_response_plan_resource(&req, CERV_REPRESENTATION_OK, &rep, now, false, &plan));
    CHECK(plan.header_len >= 17U && memcmp(plan.headers, "HTTP/1.1 200 OK\r\n", 17U) == 0);

    CHECK(cerv_response_plan_resource(&req, CERV_REPRESENTATION_NOT_FOUND, NULL, now, false, &plan));
    CHECK(plan.status == CERV_STATUS_404 && plan.send_body);
    CHECK(headers_equal(&plan,
        "HTTP/1.1 404 Not Found\r\n"
        "Date: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
        "Connection: close\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Length: 14\r\n"
        "X-Content-Type-Options: nosniff\r\n\r\n"));
    CHECK(cerv_response_plan_resource(&req, CERV_REPRESENTATION_FORBIDDEN, NULL, now, false, &plan));
    CHECK(plan.status == CERV_STATUS_403);
    CHECK(cerv_response_plan_resource(&req, CERV_REPRESENTATION_NOT_ACCEPTABLE, NULL, now, false, &plan));
    CHECK(plan.status == CERV_STATUS_406);
    CHECK(cerv_response_plan_resource(&req, CERV_REPRESENTATION_FD_EXHAUSTED_SYSTEM, NULL, now, false, &plan));
    CHECK(plan.status == CERV_STATUS_503);

    {
        pid_t child = fork();
        CHECK(child >= 0);
        if (child == 0) {
            (void)cerv_response_plan_resource(&req, CERV_REPRESENTATION_UNSUPPORTED, NULL, now, false, &plan);
            _exit(3);
        } else if (child > 0) {
            int status = 0;
            CHECK(waitpid(child, &status, 0) == child);
            CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
        }
    }

    CHECK(!cerv_response_plan_error(CERV_STATUS_416, false, now, &plan));
    CHECK(cerv_response_plan_error(CERV_STATUS_405, false, now, &plan));
    CHECK(headers_contain(&plan, "Allow: GET, HEAD\r\n"));
    CHECK(cerv_response_plan_error(CERV_STATUS_503, false, now, &plan));
    CHECK(headers_contain(&plan, "Retry-After: 1\r\nCache-Control: no-store\r\n"));
    CHECK(cerv_response_status_from_parse(CERV_PARSE_BAD_REQUEST) == CERV_STATUS_400);
    CHECK(cerv_response_status_from_parse(CERV_PARSE_NOT_IMPLEMENTED) == CERV_STATUS_501);
    CHECK(cerv_response_status_from_parse(CERV_PARSE_HTTP_VERSION_UNSUPPORTED) == CERV_STATUS_505);
}


static void test_request_to_response_pipeline(const char *root_dir)
{
    static const int64_t now = INT64_C(784111777);
    static const char request_wire[] =
        "GET /assets/app.js HTTP/1.1\r\n"
        "Host: example\r\n"
        "Accept-Encoding: gzip;q=1, br;q=0.5, identity;q=0.1\r\n"
        "Range: bytes=1-2\r\n\r\n";
    struct cerv_fs_root root = {.fd = -1};
    struct cerv_http_request request;
    struct cerv_path path;
    struct cerv_representation representation;
    struct cerv_response_plan plan;
    int transfer_fd;
    unsigned char selected_bytes[2];

    CHECK(cerv_fs_root_open(root_dir, &root) == CERV_FS_ROOT_OK);
    CHECK(cerv_http_request_parse((const unsigned char *)request_wire, sizeof(request_wire) - 1U, &request) == CERV_PARSE_OK);
    CHECK(cerv_http_target_decode_path(&request.target, &path) == CERV_TARGET_OK);
    CHECK(path.len == strlen("assets/app.js") && memcmp(path.bytes, "assets/app.js", path.len) == 0);
    CHECK(cerv_representation_select(&root, &path, &request.accept_encoding, &representation) == CERV_REPRESENTATION_OK);
    CHECK(representation.encoding == CERV_ENCODING_GZIP && representation.file.size == UINT64_C(4));
    CHECK(cerv_response_plan_resource(&request, CERV_REPRESENTATION_OK, &representation, now, false, &plan));
    CHECK(plan.status == CERV_STATUS_206 && plan.file_offset == (off_t)1 && plan.file_count == UINT64_C(2));
    CHECK(headers_contain(&plan, "Content-Encoding: gzip\r\n"));
    CHECK(headers_contain(&plan, "Content-Range: bytes 1-2/4\r\n"));
    CHECK(headers_contain(&plan, "Content-Length: 2\r\n"));

    transfer_fd = cerv_representation_take_fd(&representation);
    CHECK(transfer_fd >= 0 && representation.file.fd == -1);
    cerv_representation_close(&representation);
    CHECK(pread(transfer_fd, selected_bytes, sizeof(selected_bytes), plan.file_offset) == (ssize_t)sizeof(selected_bytes));
    CHECK(memcmp(selected_bytes, "ZI", sizeof(selected_bytes)) == 0);
    CHECK(close(transfer_fd) == 0);
    cerv_fs_root_close(&root);
}

static void test_media_types(void)
{
    static const struct {
        const char *path;
        const char *media_type;
    } cases[] = {
        {"x.HTML", "text/html; charset=utf-8"}, {"x.htm", "text/html; charset=utf-8"},
        {"x.css", "text/css; charset=utf-8"}, {"x.js", "text/javascript; charset=utf-8"},
        {"x.mjs", "text/javascript; charset=utf-8"}, {"x.json", "application/json"},
        {"x.map", "application/json"}, {"x.wasm", "application/wasm"},
        {"x.xml", "application/xml"}, {"x.txt", "text/plain; charset=utf-8"},
        {"x.svg", "image/svg+xml"}, {"x.png", "image/png"}, {"x.jpg", "image/jpeg"},
        {"x.jpeg", "image/jpeg"}, {"x.gif", "image/gif"}, {"x.webp", "image/webp"},
        {"x.avif", "image/avif"}, {"x.ico", "image/x-icon"}, {"x.woff", "font/woff"},
        {"x.woff2", "font/woff2"}, {"x.pdf", "application/pdf"}, {"x.mp4", "video/mp4"},
        {"x.webm", "video/webm"}, {"x.mp3", "audio/mpeg"}, {"x.ogg", "audio/ogg"},
        {"archive.unknown", "application/octet-stream"}, {"noext", "application/octet-stream"},
        {"dir.with.dot/file", "application/octet-stream"}, {"trailing.", "application/octet-stream"}
    };
    size_t i;

    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        struct cerv_path path = logical_path(cases[i].path);
        CHECK(strcmp(cerv_media_type_for_path(&path), cases[i].media_type) == 0);
    }
    CHECK(strcmp(cerv_media_type_for_path(NULL), "application/octet-stream") == 0);
}

int main(void)
{
    char root_template[] = "/tmp/cerv-fs-root-XXXXXX";
    char outside_template[] = "/tmp/cerv-fs-outside-XXXXXX";
    char *root_dir = mkdtemp(root_template);
    char *outside_dir = mkdtemp(outside_template);

    CHECK(root_dir != NULL);
    CHECK(outside_dir != NULL);
    if (root_dir != NULL && outside_dir != NULL) {
        test_fs_and_representation(root_dir, outside_dir);
        test_request_to_response_pipeline(root_dir);
    }
    test_linux_mount_and_magic_link_policy();
    test_media_types();
    test_response_semantics();

    if (failures != 0U) {
        fprintf(stderr, "%u failures across %u checks\n", failures, checks);
        return 1;
    }
    printf("ok: %u filesystem integration checks\n", checks);
    return 0;
}
