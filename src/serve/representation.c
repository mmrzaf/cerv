#include "serve/representation.h"

#include "base/buffer.h"
#include "serve/media_type.h"

#include <stdint.h>
#include <string.h>

_Static_assert(CERV_ETAG_WIRE_MAX >= (size_t)122, "ETag buffer must hold the fixed weak metadata validator");

struct cerv_candidate {
    enum cerv_content_encoding encoding;
    uint16_t quality;
    unsigned preference;
    bool attempted;
};

static bool cerv_append_hex64(struct cerv_buffer *buf, uint64_t value)
{
    static const unsigned char hex[] = "0123456789abcdef";
    unsigned char digits[16];
    size_t i;
    for (i = 0U; i < sizeof(digits); ++i) {
        unsigned shift = (unsigned)((15U - i) * 4U);
        digits[i] = hex[(value >> shift) & UINT64_C(0xf)];
    }
    return cerv_buffer_append(buf, digits, sizeof(digits));
}

static bool cerv_build_etag(const struct cerv_fs_file *file, unsigned char out[CERV_ETAG_WIRE_MAX], size_t *out_len)
{
    struct cerv_buffer buf;
    const uint64_t fields[] = {
        file->device, file->inode, file->size, (uint64_t)file->mtime_sec, (uint64_t)file->mtime_nsec,
        (uint64_t)file->ctime_sec, (uint64_t)file->ctime_nsec
    };
    size_t i;

    cerv_buffer_init(&buf, out, CERV_ETAG_WIRE_MAX);
    if (!cerv_buffer_append(&buf, "W/\"", 3U)) return false;
    for (i = 0U; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        if (i != 0U && !cerv_buffer_append_byte(&buf, (unsigned char)'-')) return false;
        if (!cerv_append_hex64(&buf, fields[i])) return false;
    }
    if (!cerv_buffer_append_byte(&buf, (unsigned char)'\"')) return false;
    *out_len = buf.used;
    return true;
}

static const char *cerv_suffix(enum cerv_content_encoding encoding)
{
    switch (encoding) {
    case CERV_ENCODING_IDENTITY: return "";
    case CERV_ENCODING_GZIP: return ".gz";
    case CERV_ENCODING_BR: return ".br";
    }
    return "";
}

const char *cerv_content_encoding_name(enum cerv_content_encoding encoding)
{
    switch (encoding) {
    case CERV_ENCODING_IDENTITY: return NULL;
    case CERV_ENCODING_GZIP: return "gzip";
    case CERV_ENCODING_BR: return "br";
    }
    return NULL;
}

static bool cerv_candidate_path(const struct cerv_path *logical_path, enum cerv_content_encoding encoding,
                                char out[CERV_REPRESENTATION_PATH_BYTES_MAX + 1U])
{
    const char *suffix = cerv_suffix(encoding);
    size_t suffix_len = strlen(suffix);
    if (logical_path == NULL || logical_path->len == 0U ||
        logical_path->len >= CERV_PATH_BYTES_MAX || suffix_len > 3U ||
        logical_path->len + suffix_len > CERV_REPRESENTATION_PATH_BYTES_MAX) return false;
    memcpy(out, logical_path->bytes, logical_path->len);
    memcpy(out + logical_path->len, suffix, suffix_len + 1U);
    return true;
}

static enum cerv_representation_result cerv_operational_result(enum cerv_fs_result result)
{
    switch (result) {
    case CERV_FS_FD_EXHAUSTED_PROCESS: return CERV_REPRESENTATION_FD_EXHAUSTED_PROCESS;
    case CERV_FS_FD_EXHAUSTED_SYSTEM: return CERV_REPRESENTATION_FD_EXHAUSTED_SYSTEM;
    case CERV_FS_IO: return CERV_REPRESENTATION_IO;
    case CERV_FS_UNSUPPORTED: return CERV_REPRESENTATION_UNSUPPORTED;
    case CERV_FS_OK:
    case CERV_FS_NOT_FOUND:
    case CERV_FS_POLICY_REJECTED:
    case CERV_FS_FORBIDDEN:
    case CERV_FS_NOT_REGULAR:
        break;
    }
    return CERV_REPRESENTATION_NOT_FOUND;
}

static bool cerv_is_operational(enum cerv_fs_result result)
{
    return result == CERV_FS_FD_EXHAUSTED_PROCESS || result == CERV_FS_FD_EXHAUSTED_SYSTEM ||
           result == CERV_FS_IO || result == CERV_FS_UNSUPPORTED;
}

static void cerv_sort_candidates(struct cerv_candidate candidates[3])
{
    size_t i;
    for (i = 0U; i < 3U; ++i) {
        size_t j;
        for (j = i + 1U; j < 3U; ++j) {
            if (candidates[j].quality > candidates[i].quality ||
                (candidates[j].quality == candidates[i].quality &&
                 candidates[j].preference > candidates[i].preference)) {
                struct cerv_candidate tmp = candidates[i];
                candidates[i] = candidates[j];
                candidates[j] = tmp;
            }
        }
    }
}

static enum cerv_fs_result cerv_try_candidate(const struct cerv_fs_root *root, const struct cerv_path *logical_path,
                                               enum cerv_content_encoding encoding, struct cerv_fs_file *file)
{
    char path[CERV_REPRESENTATION_PATH_BYTES_MAX + 1U];
    if (!cerv_candidate_path(logical_path, encoding, path)) return CERV_FS_NOT_FOUND;
    return cerv_fs_open_regular(root, path, file);
}

enum cerv_representation_result cerv_representation_select(const struct cerv_fs_root *root,
                                                            const struct cerv_path *logical_path,
                                                            const struct cerv_accept_encoding *accept_encoding,
                                                            struct cerv_representation *out)
{
    struct cerv_candidate candidates[3];
    bool forbidden_seen = false;
    size_t i;

    if (root == NULL || logical_path == NULL || accept_encoding == NULL || out == NULL) {
        return CERV_REPRESENTATION_IO;
    }
    *out = (struct cerv_representation){.file = {.fd = -1}};
    candidates[0] = (struct cerv_candidate){CERV_ENCODING_BR, cerv_accept_encoding_quality(accept_encoding, "br"), 3U, false};
    candidates[1] = (struct cerv_candidate){CERV_ENCODING_GZIP, cerv_accept_encoding_quality(accept_encoding, "gzip"), 2U, false};
    candidates[2] = (struct cerv_candidate){CERV_ENCODING_IDENTITY, cerv_accept_encoding_quality(accept_encoding, "identity"), 1U, false};
    cerv_sort_candidates(candidates);

    for (i = 0U; i < 3U; ++i) {
        struct cerv_fs_file file = {.fd = -1};
        enum cerv_fs_result fs_result;
        if (candidates[i].quality == 0U) continue;
        candidates[i].attempted = true;
        fs_result = cerv_try_candidate(root, logical_path, candidates[i].encoding, &file);
        if (fs_result == CERV_FS_OK) {
            out->file = file;
            out->encoding = candidates[i].encoding;
            out->media_type = cerv_media_type_for_path(logical_path);
            if (!cerv_build_etag(&out->file, out->etag, &out->etag_len)) {
                cerv_fs_file_close(&out->file);
                return CERV_REPRESENTATION_IO;
            }
            return CERV_REPRESENTATION_OK;
        }
        if (fs_result == CERV_FS_FORBIDDEN) forbidden_seen = true;
        if (cerv_is_operational(fs_result)) return cerv_operational_result(fs_result);
    }

    /* If no acceptable representation was found, probe excluded forms only to distinguish 404/403 from 406. */
    for (i = 0U; i < 3U; ++i) {
        struct cerv_fs_file file = {.fd = -1};
        enum cerv_fs_result fs_result;
        if (candidates[i].attempted) continue;
        fs_result = cerv_try_candidate(root, logical_path, candidates[i].encoding, &file);
        if (fs_result == CERV_FS_OK) {
            cerv_fs_file_close(&file);
            return CERV_REPRESENTATION_NOT_ACCEPTABLE;
        }
        if (fs_result == CERV_FS_FORBIDDEN) forbidden_seen = true;
        if (cerv_is_operational(fs_result)) return cerv_operational_result(fs_result);
    }
    return forbidden_seen ? CERV_REPRESENTATION_FORBIDDEN : CERV_REPRESENTATION_NOT_FOUND;
}

void cerv_representation_close(struct cerv_representation *representation)
{
    if (representation != NULL) cerv_fs_file_close(&representation->file);
}

int cerv_representation_take_fd(struct cerv_representation *representation)
{
    int fd;
    if (representation == NULL) return -1;
    fd = representation->file.fd;
    representation->file.fd = -1;
    return fd;
}

struct cerv_span cerv_representation_etag(const struct cerv_representation *representation)
{
    if (representation == NULL) return (struct cerv_span){.ptr = NULL, .len = 0U};
    return (struct cerv_span){.ptr = representation->etag, .len = representation->etag_len};
}
