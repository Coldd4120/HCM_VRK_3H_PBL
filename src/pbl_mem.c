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

#ifdef PBL_TXN_BACKEND
/* The shared TxnRecordV1 sources (txn_record/store/pbl) use memcpy/memcmp.
 * The freestanding PBL links -nostdlib, so provide them here. Gated by the
 * opt-in switch: the default release build does not compile or link them. */
void *memcpy(void *dst, const void *src, size_t length)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    size_t i;
    for (i = 0u; i < length; ++i) {
        d[i] = s[i];
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t length)
{
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    size_t i;
    for (i = 0u; i < length; ++i) {
        if (pa[i] != pb[i]) {
            return (int)pa[i] - (int)pb[i];
        }
    }
    return 0;
}
#endif /* PBL_TXN_BACKEND */
