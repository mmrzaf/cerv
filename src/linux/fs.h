#ifndef CERV_FS_H
#define CERV_FS_H

#include <stdint.h>

struct cerv_fs_root {
    int fd;
};

enum cerv_fs_root_result {
    CERV_FS_ROOT_OK = 0,
    CERV_FS_ROOT_INVALID,
    CERV_FS_ROOT_FORBIDDEN,
    CERV_FS_ROOT_UNSUPPORTED,
    CERV_FS_ROOT_SYSTEM
};

enum cerv_fs_result {
    CERV_FS_OK = 0,
    CERV_FS_NOT_FOUND,
    CERV_FS_POLICY_REJECTED,
    CERV_FS_FORBIDDEN,
    CERV_FS_NOT_REGULAR,
    CERV_FS_FD_EXHAUSTED_PROCESS,
    CERV_FS_FD_EXHAUSTED_SYSTEM,
    CERV_FS_IO,
    CERV_FS_UNSUPPORTED
};

struct cerv_fs_file {
    int fd;
    uint64_t device;
    uint64_t inode;
    uint64_t size;
    int64_t mtime_sec;
    uint32_t mtime_nsec;
    int64_t ctime_sec;
    uint32_t ctime_nsec;
};

enum cerv_fs_root_result cerv_fs_root_open(const char *path, struct cerv_fs_root *out);
void cerv_fs_root_close(struct cerv_fs_root *root);
enum cerv_fs_result cerv_fs_open_regular(const struct cerv_fs_root *root, const char *relative_path,
                                         struct cerv_fs_file *out);
void cerv_fs_file_close(struct cerv_fs_file *file);
enum cerv_fs_result cerv_fs_classify_errno(int error_number);

#endif
