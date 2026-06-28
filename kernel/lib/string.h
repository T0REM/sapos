/* string.h — minimal freestanding memory primitives.
 *
 * Even with -ffreestanding, the C standard lets the compiler emit calls to
 * memcpy/memset/memmove/memcmp (e.g. for struct copies or zeroing). Nothing
 * provides them in a kernel, so we must. These are the only four guaranteed to
 * be emitted, so they are all Phase 0 needs.
 */
#ifndef SCRAPOS_LIB_STRING_H
#define SCRAPOS_LIB_STRING_H

#include <stddef.h>

void *memset(void *dest, int value, size_t count);
void *memcpy(void *restrict dest, const void *restrict src, size_t count);
void *memmove(void *dest, const void *src, size_t count);
int memcmp(const void *a, const void *b, size_t count);

#endif /* SCRAPOS_LIB_STRING_H */
