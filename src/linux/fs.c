#define _GNU_SOURCE 1
#include "linux/fs.h"

#include "base/bounds.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/openat2.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define CERV_OPENAT2_RETRIES 3U

_Static_assert(sizeof(dev_t) <= sizeof(uint64_t), "dev_t must fit the normalized metadata domain");
_Static_assert(sizeof(ino_t) <= sizeof(uint64_t), "ino_t must fit the normalized metadata domain");
_Static_assert(sizeof(off_t) <= sizeof(int64_t), "off_t must fit the normalized metadata domain");
_Static_assert((off_t)-1 < (off_t)0, "Cerv requires signed off_t");
_Static_assert(sizeof(time_t) <= sizeof(int64_t), "time_t must fit the normalized metadata domain");
_Static_assert((time_t)-1 < (time_t)0, "Cerv requires signed time_t");

static const uint64_t cerv_resolve_flags =
    (uint64_t)RESOLVE_BENEATH | (uint64_t)RESOLVE_NO_SYMLINKS | (uint64_t)RESOLVE_NO_MAGICLINKS;

static int cerv_openat2_call(int dirfd, const char *path, uint64_t flags)
{
    struct open_how how = {0};
    long result;

    how.flags = flags;
    how.resolve = cerv_resolve_flags;
    result = syscall(SYS_openat2, dirfd, path, &how, sizeof(how));
    if (result < 0L) return -1;
    if (result > (long)INT_MAX) {
        /* Linux file descriptors inhabit the int domain; this is a defensive ABI check. */
        errno = EOVERFLOW;
        return -1;
    }
    return (int)result;
}

static bool cerv_relative_path_bounded(const char *path)
{
    size_t i;

    if (path == NULL || path[0] == '\0' || path[0] == '/') return false;
    for (i = 0U; i <= CERV_REPRESENTATION_PATH_BYTES_MAX; ++i) {
        if (path[i] == '\0') return true;
    }
    return false;
}

enum cerv_fs_result cerv_fs_classify_errno(int error_number)
{
    switch (error_number) {
    case ENOENT:
    case ENOTDIR:
    case ENAMETOOLONG:
        return CERV_FS_NOT_FOUND;
    case ELOOP:
    case EXDEV:
    case EAGAIN:
        return CERV_FS_POLICY_REJECTED;
    case EACCES:
    case EPERM:
        return CERV_FS_FORBIDDEN;
    case ENXIO:
    case ENODEV:
        return CERV_FS_NOT_REGULAR;
    case EMFILE:
        return CERV_FS_FD_EXHAUSTED_PROCESS;
    case ENFILE:
        return CERV_FS_FD_EXHAUSTED_SYSTEM;
    case ENOSYS:
    case EINVAL:
    case E2BIG:
        return CERV_FS_UNSUPPORTED;
    default:
        return CERV_FS_IO;
    }
}

static enum cerv_fs_result cerv_metadata_from_stat(int fd, const struct stat *st, struct cerv_fs_file *out)
{
    if (!S_ISREG(st->st_mode)) return CERV_FS_NOT_REGULAR;
    if (st->st_size < (off_t)0 || st->st_mtim.tv_nsec < 0L || st->st_mtim.tv_nsec >= 1000000000L ||
        st->st_ctim.tv_nsec < 0L || st->st_ctim.tv_nsec >= 1000000000L) {
        return CERV_FS_IO;
    }
    *out = (struct cerv_fs_file){
        .fd = fd,
        .device = (uint64_t)st->st_dev,
        .inode = (uint64_t)st->st_ino,
        .size = (uint64_t)st->st_size,
        .mtime_sec = (int64_t)st->st_mtim.tv_sec,
        .mtime_nsec = (uint32_t)st->st_mtim.tv_nsec,
        .ctime_sec = (int64_t)st->st_ctim.tv_sec,
        .ctime_nsec = (uint32_t)st->st_ctim.tv_nsec
    };
    return CERV_FS_OK;
}

enum cerv_fs_root_result cerv_fs_root_open(const char *path, struct cerv_fs_root *out)
{
    int root_fd;
    int probe_fd;
    struct stat st;

    if (out == NULL) return CERV_FS_ROOT_INVALID;
    out->fd = -1;
    if (path == NULL || path[0] == '\0') return CERV_FS_ROOT_INVALID;

    root_fd = open(path, O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (root_fd < 0) {
        if (errno == EACCES || errno == EPERM) return CERV_FS_ROOT_FORBIDDEN;
        if (errno == ENOENT || errno == ENOTDIR || errno == ENAMETOOLONG) return CERV_FS_ROOT_INVALID;
        return CERV_FS_ROOT_SYSTEM;
    }
    if (fstat(root_fd, &st) != 0 || !S_ISDIR(st.st_mode)) {
        (void)close(root_fd);
        return CERV_FS_ROOT_INVALID;
    }

    /* Startup probe: the confinement primitive and required resolution flags are mandatory. */
    probe_fd = cerv_openat2_call(root_fd, ".", (uint64_t)O_PATH | (uint64_t)O_DIRECTORY | (uint64_t)O_CLOEXEC);
    if (probe_fd < 0) {
        enum cerv_fs_result classified = cerv_fs_classify_errno(errno);
        (void)close(root_fd);
        if (classified == CERV_FS_UNSUPPORTED) return CERV_FS_ROOT_UNSUPPORTED;
        if (classified == CERV_FS_FORBIDDEN) return CERV_FS_ROOT_FORBIDDEN;
        return CERV_FS_ROOT_SYSTEM;
    }
    (void)close(probe_fd);
    out->fd = root_fd;
    return CERV_FS_ROOT_OK;
}

void cerv_fs_root_close(struct cerv_fs_root *root)
{
    if (root != NULL && root->fd >= 0) {
        (void)close(root->fd);
        root->fd = -1;
    }
}

enum cerv_fs_result cerv_fs_open_regular(const struct cerv_fs_root *root, const char *relative_path,
                                         struct cerv_fs_file *out)
{
    unsigned attempt;
    int fd = -1;
    struct stat st;
    enum cerv_fs_result metadata_result;

    if (out == NULL) return CERV_FS_POLICY_REJECTED;
    *out = (struct cerv_fs_file){.fd = -1};
    if (root == NULL || root->fd < 0 || !cerv_relative_path_bounded(relative_path)) {
        return CERV_FS_POLICY_REJECTED;
    }

    for (attempt = 0U; attempt < CERV_OPENAT2_RETRIES; ++attempt) {
        fd = cerv_openat2_call(root->fd, relative_path,
                               (uint64_t)O_RDONLY | (uint64_t)O_CLOEXEC | (uint64_t)O_NONBLOCK);
        if (fd >= 0 || errno != EAGAIN) break;
    }
    if (fd < 0) return cerv_fs_classify_errno(errno);

    if (fstat(fd, &st) != 0) {
        enum cerv_fs_result classified = cerv_fs_classify_errno(errno);
        (void)close(fd);
        return classified;
    }
    metadata_result = cerv_metadata_from_stat(fd, &st, out);
    if (metadata_result != CERV_FS_OK) {
        (void)close(fd);
        return metadata_result;
    }
    return CERV_FS_OK;
}

void cerv_fs_file_close(struct cerv_fs_file *file)
{
    if (file != NULL && file->fd >= 0) {
        (void)close(file->fd);
        file->fd = -1;
    }
}
