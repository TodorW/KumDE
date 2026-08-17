#include "jsonutil.h"

void kum_json_escape(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0)
        return;

    size_t o = 0;
    for (size_t i = 0; src[i] && o + 2 < dst_size; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') {
            dst[o++] = '\\';
            dst[o++] = (char)c;
        } else if (c == '\n' || c == '\r' || c == '\t') {
            if (o + 2 >= dst_size) break;
            dst[o++] = '\\';
            dst[o++] = c == '\n' ? 'n' : c == '\r' ? 'r' : 't';
        } else if (c < 0x20) {
            continue;
        } else {
            dst[o++] = (char)c;
        }
    }
    dst[o] = '\0';
}
