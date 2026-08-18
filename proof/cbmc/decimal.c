#include "base/checked.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern unsigned char nondet_uchar(void);
extern size_t nondet_size_t(void);
void __CPROVER_assume(bool condition);

int main(void)
{
    unsigned char bytes[4];
    size_t len = nondet_size_t();
    uint64_t value = UINT64_C(0);

    for (size_t i = 0U; i < sizeof(bytes); ++i) bytes[i] = nondet_uchar();
    __CPROVER_assume(len >= 1U && len <= sizeof(bytes));
    if (cerv_u64_decimal(bytes, len, &value)) {
        assert(value <= UINT64_C(9999));
    }
    return 0;
}
