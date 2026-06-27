/* string.c — freestanding memory primitives. See string.h for why these exist.
 *
 * Straightforward byte-at-a-time implementations: correct and obvious beats
 * clever here. They get optimised later if profiling ever says so.
 */
#include "string.h"

void *memset(void *dest, int value, size_t count) {
    unsigned char *d = (unsigned char *)dest;
    for (size_t i = 0; i < count; i++) {
        d[i] = (unsigned char)value;
    }
    return dest;
}

void *memcpy(void *restrict dest, const void *restrict src, size_t count) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < count; i++) {
        d[i] = s[i];
    }
    return dest;
}

void *memmove(void *dest, const void *src, size_t count) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    /* Copy in whichever direction avoids clobbering overlapping source bytes
     * before they are read. */
    if (d < s) {
        for (size_t i = 0; i < count; i++) {
            d[i] = s[i];
        }
    } else if (d > s) {
        for (size_t i = count; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    return dest;
}

int memcmp(const void *a, const void *b, size_t count) {
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    for (size_t i = 0; i < count; i++) {
        if (pa[i] != pb[i]) {
            return (int)pa[i] - (int)pb[i];
        }
    }
    return 0;
}
