/* slab.c — slab allocator over the buddy allocator.
 *
 * One CACHE per size class (16..2048 bytes). A cache owns a list of SLABS; a slab
 * is a single page borrowed from buddy_alloc(), carved into equal-size object
 * SLOTS for that class. kmalloc() rounds the request up to a class and hands back
 * a free slot; kfree() returns the slot to its slab. Requests bigger than the
 * largest class skip the caches entirely and buddy_alloc() whole pages directly.
 *
 * THREADED FREE LIST (no per-slot metadata).
 *   A free slot is, by definition, memory nobody is using — so we store the
 *   bookkeeping IN it. While a slot is free, its first 8 bytes hold a pointer to
 *   the next free slot in the same slab; the slab header just holds the head of
 *   that chain. Allocating pops the head (read the stored next, advance the head);
 *   freeing pushes (store the current head into the slot, point the head at it).
 *   So the free list costs zero external memory: it lives inside the very slots it
 *   tracks. This is why the smallest class is 16 bytes — every slot must be at
 *   least sizeof(void*) so the link fits. Once a slot is handed out those 8 bytes
 *   are the caller's; the link only exists while the slot is free.
 *
 * FINDING THE SLAB ON kfree (pointer -> cache).
 *   Every slab is exactly one page and page-aligned (buddy_alloc(0) returns a
 *   page-aligned frame, and the HHDM offset is page-aligned). So for ANY object
 *   pointer, masking off the low page bits lands exactly on its slab's base, where
 *   the header lives. The header's first field is a magic word that says what kind
 *   of page this is: a slab header, or the header of a large (direct-buddy) block.
 *   kfree reads that and dispatches — no global table mapping pointers to slabs is
 *   needed. (A large allocation carries the same style of header at its page base,
 *   so the identical mask-and-read trick recognises it too.)
 *
 * SINGLE-THREADED. There is no locking here. That is correct for now — we have no
 * SMP and no preemption, so nothing can run concurrently with kmalloc/kfree. When
 * Phase 4+ brings preemption (and later other CPUs), each cache gets its own lock;
 * this is deliberately deferred, not forgotten.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "slab.h"
#include "buddy.h"
#include "pmm.h"   /* PMM_FRAME_SIZE */

/* --- Layout constants ----------------------------------------------------- */

/* One page per slab. Keeping it a single page makes the "mask down to the page
 * base to find the header" trick in kfree exact: any object pointer and its slab
 * header share one page. Multi-page slabs (more slots per buddy call, better for
 * the big classes) are a later upgrade. */
#define SLAB_PAGE_SIZE PMM_FRAME_SIZE
#define SLAB_PAGE_MASK (~((uint64_t)SLAB_PAGE_SIZE - 1u))

/* All returned pointers are aligned to this. 16 is enough for any object the
 * kernel allocates (we build with no SSE, so nothing needs more). */
#define SLAB_ALIGN 16u

/* Magic words in a page's header, so kfree can tell the two kinds of page apart.
 * Arbitrary 64-bit constants — a random page is astronomically unlikely to match. */
#define SLAB_MAGIC  0x51AB51AB51AB51ABull   /* "SLABSLAB..."          */
#define LARGE_MAGIC 0x1A86A110C1A86A11ull   /* a "large alloc" marker */

/* --- Structures ----------------------------------------------------------- */

struct kmem_cache;

/* Header at the base of every slab page. The object slots follow it (aligned up
 * to SLAB_ALIGN); `free` points at the first free slot, threaded from there. */
struct slab {
    uint64_t magic;             /* SLAB_MAGIC — page-kind discriminator (offset 0) */
    struct kmem_cache *cache;   /* which cache (size class) this slab belongs to   */
    struct slab *next;          /* next slab in cache->slabs (singly linked)       */
    void *free;                 /* head of this slab's threaded free-slot list     */
    uint32_t free_count;        /* free slots in this slab                         */
    uint32_t total_slots;       /* slots this slab was carved into                 */
};

/* Header at the base of a large (> biggest class) allocation. Its first field is
 * a magic at the SAME offset as struct slab's, so kfree can read magic before it
 * knows which kind of page it is looking at. */
struct large_hdr {
    uint64_t magic;             /* LARGE_MAGIC (offset 0, mirrors struct slab)     */
    uint32_t order;             /* buddy order of the backing block, for buddy_free */
    uint32_t _pad;
};

/* One cache per size class. The aggregate counters are maintained incrementally
 * so slab_get_stats() is O(1). */
struct kmem_cache {
    size_t obj_size;            /* this class's slot size in bytes                 */
    struct slab *slabs;         /* list of all slabs this cache owns               */
    uint64_t slab_count;
    uint64_t total_slots;
    uint64_t free_slots;
};

/* --- State ---------------------------------------------------------------- */

static const size_t class_sizes[SLAB_NUM_CLASSES] = {
    16, 32, 64, 128, 256, 512, 1024, 2048,
};
static struct kmem_cache caches[SLAB_NUM_CLASSES];
static uint64_t g_hhdm;   /* HHDM offset: phys + g_hhdm = a usable pointer */

/* --- Helpers -------------------------------------------------------------- */

static inline uint64_t align_up(uint64_t v, uint64_t a) {
    return (v + (a - 1)) & ~(a - 1);
}

/* Smallest buddy order whose block is at least `bytes`. May exceed BUDDY_MAX_ORDER,
 * in which case buddy_alloc() will refuse — that is the OOM path for huge requests. */
static unsigned order_for_bytes(uint64_t bytes) {
    unsigned o = 0;
    while (((uint64_t)SLAB_PAGE_SIZE << o) < bytes) {
        o++;
    }
    return o;
}

/* Byte offset of the first slot within a slab page (the header, rounded up). */
static inline uint64_t slab_data_off(void) {
    return align_up(sizeof(struct slab), SLAB_ALIGN);
}

/* --- Slab growth ---------------------------------------------------------- */

/* Borrow one page from buddy, lay a header at its base, thread every slot onto
 * the slab's free list, and link the slab into its cache. Returns the new slab,
 * or NULL if buddy is out of pages. */
static struct slab *cache_grow(struct kmem_cache *c) {
    uint64_t phys = buddy_alloc(0);   /* one page */
    if (phys == 0) {
        return NULL;
    }
    uint64_t base = phys + g_hhdm;
    struct slab *s = (struct slab *)base;

    uint64_t data_off = slab_data_off();
    uint32_t slots = (uint32_t)((SLAB_PAGE_SIZE - data_off) / c->obj_size);

    s->magic = SLAB_MAGIC;
    s->cache = c;
    s->total_slots = slots;
    s->free_count = slots;
    s->free = NULL;

    /* Thread the free list: push every slot, so `free` ends up pointing at slot 0
     * and each slot's first 8 bytes point at the next. (Order within the slab does
     * not matter; this just walks them once.) */
    for (uint32_t i = 0; i < slots; i++) {
        void *slot = (void *)(base + data_off + (uint64_t)i * c->obj_size);
        *(void **)slot = s->free;
        s->free = slot;
    }

    s->next = c->slabs;
    c->slabs = s;
    c->slab_count++;
    c->total_slots += slots;
    c->free_slots += slots;
    return s;
}

/* --- Public: kmalloc ------------------------------------------------------ */

void slab_init(uint64_t hhdm_offset) {
    g_hhdm = hhdm_offset;
    for (int i = 0; i < SLAB_NUM_CLASSES; i++) {
        caches[i].obj_size = class_sizes[i];
        caches[i].slabs = NULL;
        caches[i].slab_count = 0;
        caches[i].total_slots = 0;
        caches[i].free_slots = 0;
    }
}

/* Direct-buddy fallback for requests too big for any size class. We prefix the
 * block with a small header (so kfree can recognise it and recover the order) and
 * return a pointer just past that header. The header sits at the page base, so
 * kfree's mask-down-to-page-base trick finds it exactly as it does for slabs. */
static void *large_alloc(size_t size) {
    uint64_t data_off = align_up(sizeof(struct large_hdr), SLAB_ALIGN);
    unsigned order = order_for_bytes(data_off + size);
    uint64_t phys = buddy_alloc(order);
    if (phys == 0) {
        return NULL;
    }
    struct large_hdr *h = (struct large_hdr *)(phys + g_hhdm);
    h->magic = LARGE_MAGIC;
    h->order = order;
    return (void *)((uint64_t)h + data_off);
}

void *kmalloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    /* Pick the smallest class that fits; fall back to buddy if nothing does. */
    int cls = -1;
    for (int i = 0; i < SLAB_NUM_CLASSES; i++) {
        if (size <= class_sizes[i]) {
            cls = i;
            break;
        }
    }
    if (cls < 0) {
        return large_alloc(size);
    }

    struct kmem_cache *c = &caches[cls];

    /* Find a slab with a free slot; grow the cache if every slab is full. */
    struct slab *s = c->slabs;
    while (s && s->free_count == 0) {
        s = s->next;
    }
    if (!s) {
        s = cache_grow(c);
        if (!s) {
            return NULL;
        }
    }

    /* Pop the head of this slab's threaded free list. */
    void *slot = s->free;
    s->free = *(void **)slot;
    s->free_count--;
    c->free_slots--;
    return slot;
}

/* --- Public: kfree -------------------------------------------------------- */

void kfree(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    /* Mask to the page base and read the magic to learn the page kind. */
    uint64_t base = (uint64_t)ptr & SLAB_PAGE_MASK;
    uint64_t magic = *(uint64_t *)base;

    if (magic == SLAB_MAGIC) {
        struct slab *s = (struct slab *)base;
        struct kmem_cache *c = s->cache;
        /* Push the slot back onto this slab's free list. */
        *(void **)ptr = s->free;
        s->free = ptr;
        s->free_count++;
        c->free_slots++;
        /* NOTE: a fully-free slab is kept, not returned to buddy. Holding empty
         * slabs avoids buddy churn and makes slot reuse deterministic; reclaiming
         * idle slabs ("reaping") is a later refinement. */
    } else if (magic == LARGE_MAGIC) {
        struct large_hdr *h = (struct large_hdr *)base;
        buddy_free(base - g_hhdm, h->order);
    }
    /* else: not a kmalloc pointer (or corrupted header) — ignore. With no panic
     * facility yet, silently dropping it is the least-harmful option. */
}

size_t kmalloc_usable_size(void *ptr) {
    if (ptr == NULL) {
        return 0;
    }
    uint64_t base = (uint64_t)ptr & SLAB_PAGE_MASK;
    uint64_t magic = *(uint64_t *)base;
    if (magic == SLAB_MAGIC) {
        return ((struct slab *)base)->cache->obj_size;
    }
    if (magic == LARGE_MAGIC) {
        unsigned order = ((struct large_hdr *)base)->order;
        return (size_t)((uint64_t)SLAB_PAGE_SIZE << order);
    }
    return 0;
}

/* --- Public: stats -------------------------------------------------------- */

void slab_get_stats(struct slab_class_stats *out) {
    for (int i = 0; i < SLAB_NUM_CLASSES; i++) {
        struct kmem_cache *c = &caches[i];
        out[i].obj_size     = c->obj_size;
        out[i].slab_count   = c->slab_count;
        out[i].total_slots  = c->total_slots;
        out[i].free_slots   = c->free_slots;
        out[i].bytes_in_use = (c->total_slots - c->free_slots) * c->obj_size;
    }
}
