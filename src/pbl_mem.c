#include <stddef.h>

void *memset(void *dst, int value, size_t length)
{
    unsigned char *p = (unsigned char *)dst;
    size_t i;
    for (i = 0u; i < length; ++i) {
        p[i] = (unsigned char)value;
    }
    return dst;
}
