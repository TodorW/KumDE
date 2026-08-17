#ifndef KUM_JSONUTIL_H
#define KUM_JSONUTIL_H

#include <stddef.h>

/* Pure string utility: no wlroots dependency, safe to unit test
 * directly (see tests/test_jsonutil.c). */
void kum_json_escape(char *dst, size_t dst_size, const char *src);

#endif
