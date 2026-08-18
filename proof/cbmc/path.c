#include "http/http_fields.h"
#include "http/http_target.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

extern unsigned char nondet_uchar(void);
extern size_t nondet_size_t(void);
void __CPROVER_assume(bool condition);

/* `cerv_http_target_decode_path` does not call authority parsing; this stub keeps
 * the translation unit closed without modeling unrelated absolute-form code. */
bool cerv_http_authority_parse(struct cerv_span in, bool allow_empty_host, struct cerv_authority *out)
{
    (void)in;
    (void)allow_empty_host;
    (void)out;
    return false;
}

int main(void)
{
    unsigned char raw[8];
    size_t len = nondet_size_t();
    struct cerv_http_target target = {0};
    struct cerv_path path;

    __CPROVER_assume(len >= 1U && len <= sizeof(raw));
    raw[0] = (unsigned char)'/';
    for (size_t i = 1U; i < sizeof(raw); ++i) raw[i] = nondet_uchar();
    target.form = CERV_TARGET_ORIGIN;
    target.raw = (struct cerv_span){.ptr = raw, .len = len};
    target.path = target.raw;
    if (cerv_http_target_decode_path(&target, &path) == CERV_TARGET_OK) {
        assert(path.len < CERV_PATH_BYTES_MAX);
        assert(path.bytes[path.len] == 0U);
        for (size_t i = 0U; i < path.len; ++i) assert(path.bytes[i] != 0U);
    }
    return 0;
}
