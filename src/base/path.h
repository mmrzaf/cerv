#ifndef CERV_PATH_H
#define CERV_PATH_H

#include <stdbool.h>
#include <stddef.h>

/* The single dot-prefixed namespace that is intentionally public (RFC 8615). */
#define CERV_WELL_KNOWN_SEGMENT ".well-known"

/*
 * True when any '/'-separated segment of the relative path begins with '.', except a leading ".well-known"
 * segment. Such paths name dotfiles or dot-directories (.env, .git/, .htpasswd, ...) that Cerv never serves.
 */
bool cerv_path_is_hidden(const unsigned char *bytes, size_t len);

/* True when the final '/'-separated segment contains a '.', i.e. it names a file with an extension. */
bool cerv_path_last_segment_has_dot(const unsigned char *bytes, size_t len);

#endif
