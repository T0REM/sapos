/* buddy.c — binary buddy allocator over physical frames.
 *
 * State is one doubly-linked free list per order. The list links live INSIDE the
 * free blocks themselves: while a block is free, the first 16 bytes at its HHDM
 * address hold a {next, prev} node; once allocated, those bytes are the caller's.
 * This is why the allocator needs no bootstrapped side metadata of its own — it
 * sidesteps the "where does the bookkeeping live" chicken-and-egg entirely (see
 * buddy.h). The one cost is that testing "is this buddy free at order k?" scans
 * order k's list instead of reading a bit; we only ever read nodes already known
 * to be free, so it is safe, and early-kernel lists are short. A side free-bitmap
 * is the standard upgrade if that scan ever hurts.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "buddy.h"
#include "pmm.h"   /* PMM_FRAME_SIZE, pmm_get_stats, pmm_claim_frame */

/* --- State ---------------------------------------------------------------- */

/* Intrusive free-list node, written at a free block's start (via the HHDM). The
 * block's ORDER is not stored here — it is implied by which list the node is on,
 * and the caller passes it back to buddy_free(). */
struct buddy_node {
    struct buddy_node *next;
    struct buddy_node *prev;
};

static struct buddy_node *free_list[BUDDY_MAX_ORDER + 1];
static uint64_t free_count[BUDDY_MAX_ORDER + 1];
static uint64_t g_hhdm;   /* HHDM offset: phys + g_hhdm = a usable pointer */

/* --- Address <-> node helpers --------------------------------------------- */

/* A free block's node lives at its physical base, reached through the HHDM. */
static inline struct buddy_node *node_at(uint64_t phys) {
    return (struct buddy_node *)(phys + g_hhdm);
}
static inline uint64_t phys_of(struct buddy_node *n) {
    return (uint64_t)n - g_hhdm;
}

/* Size in bytes of one order-`o` block: 2^o frames. */
static inline uint64_t block_bytes(unsigned o) {
    return (uint64_t)PMM_FRAME_SIZE << o;
}

/* --- Free-list primitives ------------------------------------------------- */

static void list_push(unsigned order, uint64_t phys) {
    struct buddy_node *n = node_at(phys);
    n->prev = NULL;
    n->next = free_list[order];
    if (free_list[order]) {
        free_list[order]->prev = n;
    }
    free_list[order] = n;
    free_count[order]++;
}

/* O(1) unlink — the reason the lists are doubly linked: when merging we must pull
 * the buddy out of the MIDDLE of its list, given only the node. */
static void list_remove(unsigned order, struct buddy_node *n) {
    if (n->prev) {
        n->prev->next = n->next;
    } else {
        free_list[order] = n->next;   /* n was the head */
    }
    if (n->next) {
        n->next->prev = n->prev;
    }
    free_count[order]--;
}

/* Is the block at `phys` currently free on order `order`'s list? Returns its
 * node (for removal) or NULL. Scans only known-free nodes — never touches the
 * memory of an allocated block. */
static struct buddy_node *list_find(unsigned order, uint64_t phys) {
    for (struct buddy_node *n = free_list[order]; n; n = n->next) {
        if (phys_of(n) == phys) {
            return n;
        }
    }
    return NULL;
}

/* --- Allocate (with splitting) -------------------------------------------- */

uint64_t buddy_alloc(unsigned order) {
    if (order > BUDDY_MAX_ORDER) {
        return 0;
    }

    /* Find the smallest order >= requested that has a free block. */
    unsigned o = order;
    while (o <= BUDDY_MAX_ORDER && free_list[o] == NULL) {
        o++;
    }
    if (o > BUDDY_MAX_ORDER) {
        return 0;   /* nothing big enough free */
    }

    /* Pop one block off that list. */
    struct buddy_node *n = free_list[o];
    list_remove(o, n);
    uint64_t phys = phys_of(n);

    /* Split down to the requested order. Each step halves the block: keep the
     * lower half, push the UPPER half (the buddy of the half we keep) back onto
     * the next-lower list. The pushed-back halves are exactly the ones that will
     * merge back when this allocation is later freed. */
    while (o > order) {
        o--;
        uint64_t upper = phys + block_bytes(o);
        list_push(o, upper);
    }
    return phys;
}

/* --- Free (with recursive merge) ------------------------------------------ */

void buddy_free(uint64_t phys, unsigned order) {
    if (order > BUDDY_MAX_ORDER) {
        return;
    }

    /* Coalesce upward as far as buddies keep being free. The buddy of an order-o
     * block is found by flipping bit o of the frame index — i.e. XORing the byte
     * address with (FRAME_SIZE << o). This identity is only valid because every
     * block is 2^o-aligned (guaranteed by alloc's splitting and by buddy_init's
     * chop loop), so the two buddies differ in exactly that one bit. */
    while (order < BUDDY_MAX_ORDER) {
        uint64_t buddy = phys ^ block_bytes(order);
        struct buddy_node *bn = list_find(order, buddy);
        if (!bn) {
            break;   /* buddy busy or split smaller — cannot merge further */
        }
        list_remove(order, bn);
        /* Merged block's base is the lower of the pair (its order+1 alignment). */
        if (buddy < phys) {
            phys = buddy;
        }
        order++;
    }
    list_push(order, phys);
}

/* --- Init ----------------------------------------------------------------- */

/* Chop a contiguous run of frames [first, end) into the largest aligned power-of-
 * two blocks it can hold, pushing each onto its free list. A block of order o is
 * only emitted at a frame index that is 2^o-aligned AND leaves the block fully
 * inside the run — both conditions are required for the buddy XOR to stay valid. */
static void seed_run(uint64_t first, uint64_t end) {
    uint64_t f = first;
    while (f < end) {
        unsigned order = BUDDY_MAX_ORDER;
        /* Shrink until f is 2^order-aligned and the whole block fits in the run. */
        while (order > 0 &&
               ((f & (((uint64_t)1u << order) - 1)) != 0 ||
                f + ((uint64_t)1u << order) > end)) {
            order--;
        }
        list_push(order, f * PMM_FRAME_SIZE);
        f += (uint64_t)1u << order;
    }
}

void buddy_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset,
                uint64_t reserve_frames) {
    g_hhdm = hhdm_offset;
    for (unsigned o = 0; o <= BUDDY_MAX_ORDER; o++) {
        free_list[o] = NULL;
        free_count[o] = 0;
    }

    /* Budget = how many frames to take from the pmm, leaving it `reserve_frames`
     * free for its ongoing page-table role. We claim exactly `budget` previously-
     * free frames; the rest stay free in the pmm and become its reserve. */
    uint64_t pmm_free = 0;
    pmm_get_stats(NULL, NULL, &pmm_free);
    uint64_t budget = (pmm_free > reserve_frames) ? (pmm_free - reserve_frames) : 0;
    uint64_t claimed = 0;

    /* Walk the same usable ranges the pmm parsed. For each range we accumulate
     * MAXIMAL contiguous runs of frames we can claim out of the pmm, then chop
     * each run into aligned blocks. A frame that fails to claim (already owned by
     * the pmm — its bitmap or a vmm page table) ends the current run; we never
     * take it, so it stays pmm-owned and is never double-counted. */
    for (uint64_t i = 0; i < memmap->entry_count && claimed < budget; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }
        uint64_t first = e->base / PMM_FRAME_SIZE;
        uint64_t end   = first + e->length / PMM_FRAME_SIZE;

        uint64_t f = first;
        while (f < end && claimed < budget) {
            /* Skip frames the pmm already owns (they break runs, never claimed). */
            if (!pmm_claim_frame(f * PMM_FRAME_SIZE)) {
                f++;
                continue;
            }
            /* Extend a contiguous claimed run as far as budget and free frames
             * allow. (&&-short-circuit means once budget is hit we stop BEFORE
             * claiming the next frame, so nothing leaks out of the pmm.) */
            uint64_t run_start = f;
            f++;
            claimed++;
            while (f < end && claimed < budget &&
                   pmm_claim_frame(f * PMM_FRAME_SIZE)) {
                f++;
                claimed++;
            }
            seed_run(run_start, f);
        }
    }
}

/* --- Stats ---------------------------------------------------------------- */

void buddy_get_stats(uint64_t *counts, uint64_t *total_free_bytes) {
    uint64_t total = 0;
    for (unsigned o = 0; o <= BUDDY_MAX_ORDER; o++) {
        if (counts) {
            counts[o] = free_count[o];
        }
        total += free_count[o] * block_bytes(o);
    }
    if (total_free_bytes) {
        *total_free_bytes = total;
    }
}
