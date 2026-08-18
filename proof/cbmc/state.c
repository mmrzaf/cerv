#include "runtime/conn.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

extern unsigned nondet_uint(void);
extern uint64_t nondet_u64(void);
void __CPROVER_assume(bool condition);

int main(void)
{
    struct cerv_conn conn = {0};
    struct cerv_mono_time out = {0};
    unsigned state = nondet_uint();
    uint64_t lifetime = nondet_u64();
    uint64_t header = nondet_u64();
    uint64_t write = nondet_u64();
    bool is_read;
    bool is_write;
    bool have_deadline;
    bool request_close;
    bool resource_ok;
    uint32_t requests_completed;

    __CPROVER_assume(state <= (unsigned)CERV_CONN_SEND_FALLBACK);
    conn.state = (enum cerv_conn_state)state;
    conn.lifetime_deadline.ns = lifetime;
    conn.header_deadline.ns = header;
    conn.write_deadline.ns = write;

    is_read = cerv_conn_wants_read(&conn);
    is_write = cerv_conn_wants_write(&conn);
    assert(is_read == (conn.state == CERV_CONN_RECV_HEADERS));
    assert(is_write == (conn.state == CERV_CONN_SEND_HEADERS || conn.state == CERV_CONN_SEND_BODY ||
                        conn.state == CERV_CONN_SEND_FILE || conn.state == CERV_CONN_SEND_FALLBACK));
    assert(!(is_read && is_write));

    have_deadline = cerv_conn_next_deadline(&conn, &out);
    assert(have_deadline == (conn.state != CERV_CONN_FREE));
    if (have_deadline) {
        assert(out.ns <= lifetime);
        if (is_read) {
            assert(out.ns == (header < lifetime ? header : lifetime));
        } else if (is_write) {
            assert(out.ns == (write < lifetime ? write : lifetime));
        } else {
            assert(out.ns == lifetime);
        }
    }

    request_close = (nondet_uint() & 1U) != 0U;
    resource_ok = (nondet_uint() & 1U) != 0U;
    requests_completed = nondet_uint();
    {
        bool close = cerv_conn_should_close_response(request_close, resource_ok, requests_completed);
        bool expected = request_close || !resource_ok ||
                        requests_completed >= CERV_KEEPALIVE_REQUESTS_MAX - UINT32_C(1);
        assert(close == expected);
        if (!close) assert(requests_completed < CERV_KEEPALIVE_REQUESTS_MAX - UINT32_C(1));
    }
    return 0;
}
