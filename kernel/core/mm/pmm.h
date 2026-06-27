/* pmm.h — physical frame allocator (Phase 3, step 3a).
 *
 * The bottom brick of the memory subsystem (ARCHITECTURE.md §6, step 2): it
 * hands out raw 4 KiB physical frames and takes them back. Everything above it
 * in the memory stack — paging, the buddy allocator, the slab — eats from here.
 *
 * This is the FIRST core-layer code in the project. Per ARCHITECTURE.md §4 it is
 * deliberately architecture-INDEPENDENT: tracking which physical frames are free
 * is pure bookkeeping with no x86 in it. There is no inline assembly here and
 * there never should be. The one machine-ish input — the HHDM offset and the
 * memory map — arrives as plain data from the boot layer, not as a CPU operation.
 */
#ifndef SAPOS_CORE_MM_PMM_H
#define SAPOS_CORE_MM_PMM_H

#include <stdint.h>
#include <stdbool.h>
#include "limine.h"

/* Frame size. x86_64's base page is 4 KiB; we allocate at that granularity. */
#define PMM_FRAME_SIZE 4096u

/* Walk the Limine memory map and build the bitmap-backed frame allocator.
 *   memmap      — the response Limine filled in for our memmap request.
 *   hhdm_offset — the higher-half direct map offset, so we can reach the
 *                 bitmap's physical backing store via a virtual pointer.
 * Call exactly once, early in boot, before any pmm_alloc_frame(). */
void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset);

/* Return the physical address of a free 4 KiB frame and mark it used, or 0 if
 * none remain. (Physical address 0 never sits in a usable region on PC memory
 * maps, so 0 is unambiguous as the out-of-memory sentinel.) */
uint64_t pmm_alloc_frame(void);

/* Return a frame to the pool. `phys` must be a PMM_FRAME_SIZE-aligned address
 * previously handed out by pmm_alloc_frame(). */
void pmm_free_frame(uint64_t phys);

/* Claim ONE specific frame: if it is currently free, mark it used and return
 * true; if it is already used (or out of range), return false. This is the seam
 * the buddy allocator uses to take ownership of frames out of the pmm at init,
 * so a frame is never owned by both allocators (see buddy.h ownership note).
 * `phys` must be PMM_FRAME_SIZE-aligned. */
bool pmm_claim_frame(uint64_t phys);

/* Report frame accounting. Any pointer may be NULL if that figure isn't wanted.
 *   total — usable frames the allocator manages (free + used)
 *   used  — usable frames currently handed out (includes the bitmap's own)
 *   free  — usable frames available right now */
void pmm_get_stats(uint64_t *total, uint64_t *used, uint64_t *free);

#endif /* SAPOS_CORE_MM_PMM_H */
