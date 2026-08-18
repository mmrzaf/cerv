#include "http/http_request.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct expected_name {
    const char *name;
    enum cerv_parse_result result;
};

static enum cerv_parse_result result_from_name(const char *name, bool *ok)
{
    static const struct expected_name names[] = {
        {"INCOMPLETE", CERV_PARSE_INCOMPLETE},
        {"OK", CERV_PARSE_OK},
        {"400", CERV_PARSE_BAD_REQUEST},
        {"414", CERV_PARSE_URI_TOO_LONG},
        {"431", CERV_PARSE_HEADERS_TOO_LARGE},
        {"405", CERV_PARSE_METHOD_NOT_ALLOWED},
        {"501", CERV_PARSE_NOT_IMPLEMENTED},
        {"505", CERV_PARSE_HTTP_VERSION_UNSUPPORTED},
        {"417", CERV_PARSE_EXPECTATION_FAILED}
    };
    size_t i = 0U;
    for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (strcmp(name, names[i].name) == 0) {
            *ok = true;
            return names[i].result;
        }
    }
    *ok = false;
    return CERV_PARSE_BAD_REQUEST;
}

static unsigned char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    long end = 0L;
    unsigned char *data = NULL;

    if (f == NULL || fseek(f, 0L, SEEK_END) != 0) {
        if (f != NULL) (void)fclose(f);
        return NULL;
    }
    end = ftell(f);
    if (end < 0L || (unsigned long)end > (unsigned long)SIZE_MAX || fseek(f, 0L, SEEK_SET) != 0) {
        (void)fclose(f);
        return NULL;
    }
    *len = (size_t)end;
    data = malloc(*len == 0U ? 1U : *len);
    if (data == NULL) {
        (void)fclose(f);
        return NULL;
    }
    if (*len != 0U && fread(data, 1U, *len, f) != *len) {
        free(data);
        (void)fclose(f);
        return NULL;
    }
    if (fclose(f) != 0) {
        free(data);
        return NULL;
    }
    return data;
}

int main(int argc, char **argv)
{
    char manifest_path[512];
    FILE *manifest = NULL;
    char line[512];
    unsigned total = 0U;
    unsigned failed = 0U;

    if (argc != 2) {
        fprintf(stderr, "usage: %s CORPUS_DIR\n", argv[0]);
        return 2;
    }
    {
        int written = snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.tsv", argv[1]);
        if (written < 0 || (size_t)written >= sizeof(manifest_path)) {
            fprintf(stderr, "corpus path too long\n");
            return 2;
        }
    }
    manifest = fopen(manifest_path, "r");
    if (manifest == NULL) {
        fprintf(stderr, "cannot open %s: %s\n", manifest_path, strerror(errno));
        return 2;
    }
    while (fgets(line, sizeof(line), manifest) != NULL) {
        char expected_text[32];
        char filename[384];
        char path[768];
        bool known = false;
        enum cerv_parse_result expected;
        enum cerv_parse_result actual;
        struct cerv_http_request req;
        unsigned char *data = NULL;
        size_t len = 0U;

        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, "%31s %383s", expected_text, filename) != 2) {
            fprintf(stderr, "bad manifest line: %s", line);
            ++failed;
            continue;
        }
        expected = result_from_name(expected_text, &known);
        if (!known) {
            fprintf(stderr, "unknown result %s\n", expected_text);
            ++failed;
            continue;
        }
        {
            int written = snprintf(path, sizeof(path), "%s/%s", argv[1], filename);
            if (written < 0 || (size_t)written >= sizeof(path)) {
                fprintf(stderr, "path too long for %s\n", filename);
                ++failed;
                continue;
            }
        }
        data = read_file(path, &len);
        if (data == NULL) {
            fprintf(stderr, "cannot read %s\n", path);
            ++failed;
            continue;
        }
        actual = cerv_http_request_parse(data, len, &req);
        ++total;
        if (actual != expected) {
            fprintf(stderr, "FAIL %-32s expected %d got %d\n", filename, (int)expected, (int)actual);
            ++failed;
        }
        free(data);
    }
    if (ferror(manifest) != 0) {
        fprintf(stderr, "manifest read error\n");
        ++failed;
    }
    if (fclose(manifest) != 0) {
        fprintf(stderr, "manifest close error\n");
        ++failed;
    }
    if (failed != 0U) {
        fprintf(stderr, "%u adversarial corpus failures (%u cases)\n", failed, total);
        return 1;
    }
    printf("ok: %u adversarial request corpus cases\n", total);
    return 0;
}
