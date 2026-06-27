/* pmm.c — physical frame allocator backed by a bitmap.
 *
 * One bit per 4 KiB frame: 1 = used, 0 = free. The bitmap lives in physical RAM
 * (inside a usable region we then reserve) and we reach it through the HHDM, so
 * all writes go to `phys + hhdm_offset`. See pmm.h for the layering rationale.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "pmm.h"
#include "lib/string.h"   /* memset — to paint the bitmap all-used at the start */

/* --- Allocator state ------------------------------------------------------ */

/* Pointer to the bitmap's first byte, already biased through the HHDM so it is
 * a virtual address the CPU can dereference. */
static uint8_t *bitmap;
static uint64_t bitmap_bytes;   /* size of the bitmap in bytes               */

static uint64_t total_frames;   /* frames the bitmap spans (incl. holes)     */
static uint64_t usable_frames;  /* frames in USABLE regions (the real total) */
static uint64_t free_frames;    /* usable frames currently free              */

/* Where the next first-fit scan begins. Not required for correctness (we could
 * always scan from 0), but freeing low and re-allocating stays cheap this way. */
static uint64_t next_hint;

/* --- Bit helpers (frame index <-> bitmap bit) ----------------------------- */

static inline void bit_set(uint64_t i)   { bitmap[i / 8] |=  (uint8_t)(1u << (i % 8)); }
static inline void bit_clear(uint64_t i) { bitmap[i / 8] &= (uint8_t)~(1u << (i % 8)); }
static inline bool bit_test(uint64_t i)  { return (bitmap[i / 8] >> (i % 8)) & 1u; }

/* --- Init ----------------------------------------------------------------- */

void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset) {
    /* Pass 1: find the highest address covered by any USABLE region. The bitmap
     * only needs to span up to here — frames beyond the last usable byte can
     * never be allocated, so there is no point tracking them. */
    uint64_t highest_addr = 0;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }
        uint64_t top = e->base + e->length;
        if (top > highest_addr) {
            highest_addr = top;
        }
    }

    total_frames = highest_addr / PMM_FRAME_SIZE;
    /* One bit per frame, rounded up to whole bytes. */
    bitmap_bytes = (total_frames + 7) / 8;

    /* Pass 2: find a USABLE region big enough to hold the bitmap, and park it at
     * that region's base. This is the bootstrap — the allocator's own storage is
     * carved from the very memory it is about to manage, because we have no other
     * allocator to ask (pmm.h explains the chicken-and-egg). */
    uint64_t bitmap_phys = 0;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE && e->length >= bitmap_bytes) {
            bitmap_phys = e->base;
            break;
        }
    }
    /* No region can hold the bitmap — unrecoverable this early; refuse to run. */
    if (bitmap_phys == 0) {
        for (;;) { __asm__ volatile ("hlt"); }
    }

    /* The only machine-touching line in the file, and it is not assembly: turn a
     * physical address into a usable pointer by adding the HHDM offset. */
    bitmap = (uint8_t *)(bitmap_phys + hhdm_offset);

    /* Start pessimistic: every frame used. Then we punch holes for free RAM.
     * This way every non-USABLE byte (reserved, ACPI, MMIO, kernel, ...) stays
     * marked used for free, since we simply never clear it. */
    memset(bitmap, 0xFF, bitmap_bytes);

    /* Pass 3: clear the bits for every USABLE frame, counting them as we go.
     * Limine guarantees USABLE regions are 4 KiB-aligned in base and length, so
     * the division is exact and no partial frames sneak in. */
    usable_frames = 0;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }
        uint64_t start = e->base / PMM_FRAME_SIZE;
        uint64_t count = e->length / PMM_FRAME_SIZE;
        for (uint64_t f = 0; f < count; f++) {
            bit_clear(start + f);
            usable_frames++;
        }
    }
    free_frames = usable_frames;

    /* Pass 4: re-reserve the frames the bitmap itself occupies. They sit inside
     * the usable region we just freed, so without this the allocator would hand
     * out its own backing store and corrupt itself. */
    uint64_t bitmap_first = bitmap_phys / PMM_FRAME_SIZE;
    uint64_t bitmap_count = (bitmap_bytes + PMM_FRAME_SIZE - 1) / PMM_FRAME_SIZE;
    for (uint64_t f = 0; f < bitmap_count; f++) {
        bit_set(bitmap_first + f);
        free_frames--;
    }

    next_hint = 0;
}

/* --- Allocate / free ------------------------------------------------------ */

uint64_t pmm_alloc_frame(void) {
    if (free_frames == 0) {
        return 0;
    }

    /* First-fit: scan from the hint to the end, then wrap to cover [0, hint).
     * The hint just avoids rescanning low frames we know are taken; correctness
     * does not depend on it. */
    for (uint64_t pass = 0; pass < 2; pass++) {
        uint64_t start = (pass == 0) ? next_hint : 0;
        uint64_t end   = (pass == 0) ? total_frames : next_hint;
        for (uint64_t i = start; i < end; i++) {
            if (!bit_test(i)) {
                bit_set(i);
                free_frames--;
                next_hint = i + 1;
                return i * PMM_FRAME_SIZE;
            }
        }
    }

    /* free_frames said there was room but the scan found none — would mean the
     * count and the bitmap disagree, i.e. a bug. Report OOM rather than lie. */
    return 0;
}

void pmm_free_frame(uint64_t phys) {
    uint64_t i = phys / PMM_FRAME_SIZE;

    /* Ignore double-frees: only count a frame back as free if it was used. */
    if (!bit_test(i)) {
        return;
    }
    bit_clear(i);
    free_frames++;

    /* Let the next allocation reuse this frame immediately if it is the lowest
     * free one — keeps freed memory from drifting to the end of the scan. */
    if (i < next_hint) {
        next_hint = i;
    }
}

/* --- Stats ---------------------------------------------------------------- */

void pmm_get_stats(uint64_t *total, uint64_t *used, uint64_t *free) {
    if (total) { *total = usable_frames; }
    if (used)  { *used  = usable_frames - free_frames; }
    if (free)  { *free  = free_frames; }
}
