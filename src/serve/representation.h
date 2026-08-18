#ifndef CERV_REPRESENTATION_H
#define CERV_REPRESENTATION_H

#include "linux/fs.h"
#include "http/http_fields.h"
#include "http/http_target.h"
#include "base/slice.h"

#include <stdbool.h>
#include <stddef.h>

#define CERV_ETAG_WIRE_MAX ((size_t)128)

enum cerv_content_encoding {
    CERV_ENCODING_IDENTITY = 0,
    CERV_ENCODING_GZIP,
    CERV_ENCODING_BR
};

enum cerv_representation_result {
    CERV_REPRESENTATION_OK = 0,
    CERV_REPRESENTATION_NOT_FOUND,
    CERV_REPRESENTATION_FORBIDDEN,
    CERV_REPRESENTATION_NOT_ACCEPTABLE,
    CERV_REPRESENTATION_FD_EXHAUSTED_PROCESS,
    CERV_REPRESENTATION_FD_EXHAUSTED_SYSTEM,
    CERV_REPRESENTATION_IO,
    CERV_REPRESENTATION_UNSUPPORTED
};

struct cerv_representation {
    struct cerv_fs_file file;
    enum cerv_content_encoding encoding;
    const char *media_type;
    unsigned char etag[CERV_ETAG_WIRE_MAX];
    size_t etag_len;
};

enum cerv_representation_result cerv_representation_select(const struct cerv_fs_root *root,
                                                            const struct cerv_path *logical_path,
                                                            const struct cerv_accept_encoding *accept_encoding,
                                                            struct cerv_representation *out);
void cerv_representation_close(struct cerv_representation *representation);
int cerv_representation_take_fd(struct cerv_representation *representation);
struct cerv_span cerv_representation_etag(const struct cerv_representation *representation);
const char *cerv_content_encoding_name(enum cerv_content_encoding encoding);

#endif
