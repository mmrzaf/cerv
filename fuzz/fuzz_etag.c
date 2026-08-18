#include "http/http_fields.h"
#include "fuzz_support.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct cerv_span input = {.ptr = data, .len = size};
    struct cerv_entity_tag tag;
    if (cerv_entity_tag_parse(input, &tag) == CERV_ETAG_OK) {
        cerv_fuzz_require(cerv_fuzz_span_within(data, size, tag.opaque));
        cerv_fuzz_require(cerv_entity_tag_weak_equal(tag, tag));
        cerv_fuzz_require(cerv_entity_tag_strong_equal(tag, tag) == !tag.weak);
        (void)cerv_if_none_match_matches(input, tag);
    }
    (void)cerv_if_none_match_valid(input);
    return 0;
}
