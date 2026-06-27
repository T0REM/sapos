/* buddy.h — binary buddy allocator (ARCHITECTURE.md §6, step 3c / 4).
 *
 * Page-granularity allocation with splitting and coalescing, sitting on top of
 * the physical frame allocator (pmm). It hands out blocks of 2^order frames
 * (order 0 = one 4 KiB frame, up to BUDDY_MAX_ORDER = 4 MiB), splitting a larger
 * free block when an exact size is missing and merging buddies back together on
 * free so large contiguous regions reappear.
 *
 * Like the pmm (see pmm.h §4 rationale) this is deliberately architecture-
 * INDEPENDENT: it is pure power-of-two bookkeeping over frame indices, with no
 * x86 in it and no inline assembly. The only machine-ish input is the HHDM
 * offset, which arrives as plain data so we can reach a free block's backing
 * store through a virtual pointer.
 *
 * OWNERSHIP (the key correctness point): the buddy allocator OWNS the bulk of
 * usable physical memory going forward. The pmm is kept alive only for single-
 * frame internal needs (page tables via the vmm) out of a small reserve. The two
 * never own the same frame — buddy_init() claims each frame it manages out of the
 * pmm (marking it used there) so the pmm can never re-hand it. See buddy.c.
 */
#ifndef SAPOS_CORE_MM_BUDDY_H
#define SAPOS_CORE_MM_BUDDY_H

#include <stdint.h>
#include "limine.h"

/* Largest block the allocator tracks: 2^10 frames = 1024 * 4 KiB = 4 MiB. One
 * free list exists per order, 0..BUDDY_MAX_ORDER inclusive. */
#define BUDDY_MAX_ORDER 10

/* Frames to leave free in the pmm for its ongoing single-frame role (page
 * tables). 1024 frames = 4 MiB — tiny against real RAM, plenty for page tables
 * for a long time. buddy_init() claims everything else. */
#define BUDDY_PMM_RESERVE_FRAMES 1024u

/* Take ownership of usable physical memory and populate the free lists.
 *   memmap         — the same Limine memmap the pmm parsed (the usable ranges).
 *   hhdm_offset    — higher-half direct map offset, to reach free blocks' links.
 *   reserve_frames — frames to leave free in the pmm (its page-table reserve).
 * Call once, after pmm_init() and vmm_init(), interrupts still masked. */
void buddy_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset,
                uint64_t reserve_frames);

/* Allocate a 2^order block. Returns its physical address, or 0 if none is
 * available (0 is never a usable frame, so it is an unambiguous OOM sentinel).
 * Splits a larger free block down when no block of exactly `order` is free. */
uint64_t buddy_alloc(unsigned order);

/* Free a block previously returned by buddy_alloc() at the SAME order. Merges
 * with its buddy (and onward, recursively) whenever the buddy is also free. */
void buddy_free(uint64_t phys, unsigned order);

/* Report free-list state. Either pointer may be NULL.
 *   counts           — array of length BUDDY_MAX_ORDER+1; counts[o] = free
 *                      blocks currently on order o's list.
 *   total_free_bytes — sum of all free blocks' sizes. */
void buddy_get_stats(uint64_t *counts, uint64_t *total_free_bytes);

#endif /* SAPOS_CORE_MM_BUDDY_H */
