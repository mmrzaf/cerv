#include "runtime/conn.h"

#include <stdbool.h>
#include <stdint.h>

bool cerv_conn_wants_read(const struct cerv_conn *conn)
{
    return conn != NULL && conn->state == CERV_CONN_RECV_HEADERS;
}

bool cerv_conn_wants_write(const struct cerv_conn *conn)
{
    if (conn == NULL) return false;
    return conn->state == CERV_CONN_SEND_HEADERS || conn->state == CERV_CONN_SEND_BODY ||
           conn->state == CERV_CONN_SEND_FILE || conn->state == CERV_CONN_SEND_FALLBACK;
}

bool cerv_conn_next_deadline(const struct cerv_conn *conn, struct cerv_mono_time *out)
{
    uint64_t next;
    if (conn == NULL || out == NULL || conn->state == CERV_CONN_FREE) return false;
    next = conn->lifetime_deadline.ns;
    if (conn->state == CERV_CONN_RECV_HEADERS && conn->header_deadline.ns < next) {
        next = conn->header_deadline.ns;
    } else if (cerv_conn_wants_write(conn) && conn->write_deadline.ns < next) {
        next = conn->write_deadline.ns;
    }
    out->ns = next;
    return true;
}


bool cerv_conn_should_close_response(bool request_close, bool resource_ok, uint32_t requests_completed)
{
    return request_close || !resource_ok ||
           requests_completed >= CERV_KEEPALIVE_REQUESTS_MAX - UINT32_C(1);
}
