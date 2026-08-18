#include "http/http_fields.h"
#include "fuzz_support.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static bool cerv_ae_equal(const struct cerv_accept_encoding *a, const struct cerv_accept_encoding *b)
{
    return a->present == b->present && a->br_seen == b->br_seen &&
           a->gzip_seen == b->gzip_seen && a->identity_seen == b->identity_seen &&
           a->wildcard_seen == b->wildcard_seen && a->br_q == b->br_q &&
           a->gzip_q == b->gzip_q && a->identity_q == b->identity_q &&
           a->wildcard_q == b->wildcard_q;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct cerv_accept_encoding state;
    struct cerv_accept_encoding before;
    bool ok;

    cerv_accept_encoding_init(&state);
    before = state;
    ok = cerv_accept_encoding_add_field(&state, (struct cerv_span){.ptr = data, .len = size});
    if (!ok) {
        cerv_fuzz_require(cerv_ae_equal(&state, &before));
    } else {
        cerv_fuzz_require(state.present);
        cerv_fuzz_require(cerv_accept_encoding_quality(&state, "br") <= 1000U);
        cerv_fuzz_require(cerv_accept_encoding_quality(&state, "gzip") <= 1000U);
        cerv_fuzz_require(cerv_accept_encoding_quality(&state, "identity") <= 1000U);
    }
    return 0;
}
