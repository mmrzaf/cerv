#ifndef CERV_HTTP_FIELDS_H
#define CERV_HTTP_FIELDS_H

#include "http/http_date.h"
#include "base/slice.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct cerv_authority {
    struct cerv_span host;
    struct cerv_span port;
    bool has_port;
    bool ip_literal;
};

bool cerv_http_is_token(struct cerv_span in);
bool cerv_http_field_value_valid(struct cerv_span in);
bool cerv_http_authority_parse(struct cerv_span in, bool allow_empty_host, struct cerv_authority *out);
bool cerv_http_content_length_parse(struct cerv_span in, uint64_t *out);

enum cerv_q_parse_result {
    CERV_Q_OK = 0,
    CERV_Q_INVALID
};

enum cerv_q_parse_result cerv_http_qvalue_parse(struct cerv_span in, uint16_t *out);

struct cerv_accept_encoding {
    bool present;
    bool br_seen;
    bool gzip_seen;
    bool identity_seen;
    bool wildcard_seen;
    uint16_t br_q;
    uint16_t gzip_q;
    uint16_t identity_q;
    uint16_t wildcard_q;
};

void cerv_accept_encoding_init(struct cerv_accept_encoding *out);
bool cerv_accept_encoding_add_field(struct cerv_accept_encoding *state, struct cerv_span value);
uint16_t cerv_accept_encoding_quality(const struct cerv_accept_encoding *state, const char *coding);

enum cerv_etag_parse_result {
    CERV_ETAG_OK = 0,
    CERV_ETAG_INVALID
};

struct cerv_entity_tag {
    bool weak;
    struct cerv_span opaque;
};

enum cerv_etag_parse_result cerv_entity_tag_parse(struct cerv_span in, struct cerv_entity_tag *out);
bool cerv_entity_tag_weak_equal(struct cerv_entity_tag a, struct cerv_entity_tag b);
bool cerv_entity_tag_strong_equal(struct cerv_entity_tag a, struct cerv_entity_tag b);
bool cerv_if_none_match_valid(struct cerv_span in);
bool cerv_if_none_match_matches(struct cerv_span in, struct cerv_entity_tag current);

enum cerv_if_range_parse_result {
    CERV_IF_RANGE_ETAG = 0,
    CERV_IF_RANGE_DATE,
    CERV_IF_RANGE_INVALID
};

struct cerv_if_range_value {
    enum cerv_if_range_parse_result kind;
    struct cerv_entity_tag etag;
    struct cerv_http_date date;
};

enum cerv_if_range_parse_result cerv_http_if_range_parse(struct cerv_span in, int current_year,
                                                          struct cerv_if_range_value *out);

#endif
