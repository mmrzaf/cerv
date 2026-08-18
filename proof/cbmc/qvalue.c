#include "http/http_fields.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern unsigned char nondet_uchar(void);
extern size_t nondet_size_t(void);
void __CPROVER_assume(bool condition);

int main(void)
{
    unsigned char bytes[5];
    size_t len = nondet_size_t();
    uint16_t q = 0U;

    for (size_t i = 0U; i < sizeof(bytes); ++i) bytes[i] = nondet_uchar();
    __CPROVER_assume(len >= 1U && len <= sizeof(bytes));
    if (cerv_http_qvalue_parse((struct cerv_span){.ptr = bytes, .len = len}, &q) == CERV_Q_OK) {
        assert(q <= 1000U);
    }
    return 0;
}
