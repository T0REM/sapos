/* slab.h — slab allocator: kmalloc/kfree for small objects (Phase 3, step 3d).
 *
 * The LAST brick of the memory subsystem (ARCHITECTURE.md §6, step 5). It sits on
 * top of the buddy allocator and turns its page-granularity blocks into a pool of
 * small, fixed-size objects, exposing the general-purpose kmalloc/kfree the rest
 * of the kernel will use for odds-and-ends allocation.
 *
 * Like the pmm and buddy below it (see their headers' §4 rationale) this is
 * deliberately architecture-INDEPENDENT: it is pure bookkeeping — size classes,
 * free lists, slab headers — with no x86 in it and no inline assembly. Its only
 * machine-ish input is the HHDM offset, which arrives as plain data so it can turn
 * a buddy block's physical address into a usable virtual pointer.
 *
 * OWNERSHIP: the slab never touches the pmm directly. Every byte it manages comes
 * from buddy_alloc() and goes back via buddy_free() (see slab.c). The buddy owns
 * the pages; the slab only subdivides the ones it has borrowed.
 */
#ifndef SCRAPOS_CORE_MM_SLAB_H
#define SCRAPOS_CORE_MM_SLAB_H

#include <stddef.h>
#include <stdint.h>

/* Fixed size classes, in bytes: 16, 32, 64, 128, 256, 512, 1024, 2048. A request
 * is rounded up to the smallest class that fits; anything larger than the biggest
 * class falls back to a direct buddy_alloc of whole pages (see kmalloc). */
#define SLAB_NUM_CLASSES 8

/* Wire in the HHDM offset and zero the caches. Call once, after buddy_init(),
 * before the first kmalloc(). (kmalloc also lazily tolerates being called before
 * a slab exists by growing on demand, but the offset must be set here first.) */
void slab_init(uint64_t hhdm_offset);

/* Allocate at least `size` bytes, 16-byte aligned, or NULL on failure / size 0.
 * size <= 2048 is served from the matching size-class cache (growing it with a
 * fresh slab from buddy_alloc if it has no free slot); size > 2048 falls back to
 * a direct buddy_alloc of enough whole pages. kfree() handles both cases. */
void *kmalloc(size_t size);

/* Return a pointer previously handed out by kmalloc() to its pool (or to the
 * buddy allocator, for the large fallback case). kfree(NULL) is a safe no-op. */
void kfree(void *ptr);

/* Usable size of a live kmalloc() pointer: the size class it was rounded up to,
 * or the byte size of the backing block for a large allocation. Mostly a debug /
 * self-test aid (lets a caller confirm which class a pointer landed in). */
size_t kmalloc_usable_size(void *ptr);

/* Per-size-class accounting, filled by slab_get_stats(). */
struct slab_class_stats {
    size_t   obj_size;      /* the size class, in bytes                         */
    uint64_t slab_count;    /* slabs (pages) this cache currently owns          */
    uint64_t total_slots;   /* object slots across all those slabs              */
    uint64_t free_slots;    /* slots currently free                             */
    uint64_t bytes_in_use;  /* (total_slots - free_slots) * obj_size            */
};

/* Fill `out`, an array of length SLAB_NUM_CLASSES, with each class's stats. */
void slab_get_stats(struct slab_class_stats *out);

#endif /* SCRAPOS_CORE_MM_SLAB_H */
