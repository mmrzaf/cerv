#include "base/bounds.h"
#include "base/path.h"
#include "http/http_target.h"
#include "fuzz_support.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct cerv_http_target target;
    enum cerv_target_result result = cerv_http_target_parse(
        (struct cerv_span){.ptr = data, .len = size}, &target);

    if (result == CERV_TARGET_OK) {
        cerv_fuzz_require(size <= CERV_REQUEST_LINE_MAX);
        cerv_fuzz_require(cerv_fuzz_span_within(data, size, target.raw));
        if (target.authority.ptr != NULL) cerv_fuzz_require(cerv_fuzz_span_within(data, size, target.authority));
        if (target.path.ptr != NULL) cerv_fuzz_require(cerv_fuzz_span_within(data, size, target.path));
        if (target.query.ptr != NULL) cerv_fuzz_require(cerv_fuzz_span_within(data, size, target.query));
        if (target.form == CERV_TARGET_ORIGIN || target.form == CERV_TARGET_ABSOLUTE) {
            struct cerv_path path;
            enum cerv_target_result dr = cerv_http_target_decode_path(&target, &path);
            if (dr == CERV_TARGET_OK) {
                cerv_fuzz_require(path.len < CERV_PATH_BYTES_MAX);
                cerv_fuzz_require(path.bytes[path.len] == 0U);
                cerv_fuzz_require(memchr(path.bytes, 0, path.len) == NULL);
                if (path.len != 0U) cerv_fuzz_require(path.bytes[0] != (unsigned char)'/');
                if (path.directory_index) {
                    cerv_fuzz_require(path.len >= CERV_INDEX_NAME_LEN);
                    cerv_fuzz_require(memcmp(path.bytes + path.len - CERV_INDEX_NAME_LEN, CERV_INDEX_NAME, CERV_INDEX_NAME_LEN) == 0);
                }
                /* The policy predicates are total over every decodable path. */
                (void)cerv_path_is_hidden(path.bytes, path.len);
                (void)cerv_path_last_segment_has_dot(path.bytes, path.len);
            }
        }
    }
    return 0;
}
