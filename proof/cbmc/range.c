#include "http/http_range.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

extern unsigned nondet_uint(void);
extern uint64_t nondet_u64(void);
void __CPROVER_assume(bool condition);

int main(void)
{
    unsigned kind = nondet_uint();
    uint64_t first = nondet_u64();
    uint64_t second = nondet_u64();
    uint64_t length = nondet_u64();
    struct cerv_range_spec spec;
    struct cerv_range_selection selected;

    __CPROVER_assume(kind <= 2U);
    __CPROVER_assume(first <= UINT64_C(15));
    __CPROVER_assume(second <= UINT64_C(15));
    __CPROVER_assume(length <= UINT64_C(15));
    spec = (struct cerv_range_spec){.kind = (enum cerv_range_kind)kind, .first = first, .second = second};
    if (cerv_http_range_normalize(spec, length, &selected) == CERV_RANGE_SATISFIABLE) {
        assert(length != UINT64_C(0));
        assert(selected.start <= selected.end);
        assert(selected.end < length);
        assert(selected.count == selected.end - selected.start + UINT64_C(1));
    }
    return 0;
}
