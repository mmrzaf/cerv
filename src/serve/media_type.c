#include "serve/media_type.h"

#include <stddef.h>
#include <string.h>

struct cerv_media_entry {
    const char *extension;
    const char *media_type;
};

static bool cerv_ascii_extension_equal(const unsigned char *p, size_t len, const char *literal)
{
    size_t i;
    size_t literal_len = strlen(literal);
    if (len != literal_len) return false;
    for (i = 0U; i < len; ++i) {
        unsigned char a = p[i];
        unsigned char b = (unsigned char)literal[i];
        if (a >= (unsigned char)'A' && a <= (unsigned char)'Z') a = (unsigned char)(a + 32U);
        if (a != b) return false;
    }
    return true;
}

const char *cerv_media_type_for_path(const struct cerv_path *path)
{
    static const struct cerv_media_entry table[] = {
        {"html", "text/html; charset=utf-8"}, {"htm", "text/html; charset=utf-8"},
        {"css", "text/css; charset=utf-8"}, {"js", "text/javascript; charset=utf-8"},
        {"mjs", "text/javascript; charset=utf-8"}, {"json", "application/json"},
        {"map", "application/json"}, {"wasm", "application/wasm"},
        {"xml", "application/xml"}, {"txt", "text/plain; charset=utf-8"},
        {"svg", "image/svg+xml"}, {"png", "image/png"}, {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"}, {"gif", "image/gif"}, {"webp", "image/webp"},
        {"avif", "image/avif"}, {"ico", "image/x-icon"}, {"woff", "font/woff"},
        {"woff2", "font/woff2"}, {"pdf", "application/pdf"}, {"mp4", "video/mp4"},
        {"webm", "video/webm"}, {"mp3", "audio/mpeg"}, {"ogg", "audio/ogg"}
    };
    size_t dot = 0U;
    size_t i;

    if (path == NULL || path->len == 0U) return "application/octet-stream";
    for (i = path->len; i > 0U; --i) {
        unsigned char c = path->bytes[i - 1U];
        if (c == (unsigned char)'/') break;
        if (c == (unsigned char)'.') {
            dot = i;
            break;
        }
    }
    if (dot == 0U || dot >= path->len) return "application/octet-stream";
    for (i = 0U; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (cerv_ascii_extension_equal(path->bytes + dot, path->len - dot, table[i].extension)) {
            return table[i].media_type;
        }
    }
    return "application/octet-stream";
}
