/* paging.c — the x86_64 4-level page-table walk and CPU paging controls.
 *
 * This file is where the 48-bit virtual address actually gets taken apart and
 * walked. See paging.h for the layering rationale; see vmm.c for the policy
 * (what gets mapped) that drives these primitives.
 *
 * The address split (bits of a canonical 48-bit VA):
 *   [47:39] PML4 index   [38:30] PDPT index   [29:21] PD index
 *   [20:12] PT index     [11:0]  page offset
 * Each table holds 512 8-byte entries (= one 4 KiB frame). An entry stores the
 * next table's / final frame's physical address in bits [51:12] plus flags in
 * [11:0]. We reach a table's bytes through the HHDM: virt = phys + hhdm_offset.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "paging.h"
#include "core/mm/pmm.h"
#include "lib/string.h"   /* memset — to zero freshly allocated tables */

/* --- Hardware PTE bits ----------------------------------------------------- */

#define PTE_PRESENT (1ull << 0)   /* entry is valid; absent => walk stops here */
#define PTE_WRITE   (1ull << 1)   /* writes allowed through this entry         */
#define PTE_USER    (1ull << 2)   /* ring 3 may access (else supervisor-only)  */

/* The physical address a table/PTE entry points at lives in bits [51:12]. This
 * mask extracts it, discarding both the low flag bits and any high bits. */
#define PTE_ADDR_MASK 0x000ffffffffff000ull

/* Pull the 9-bit index for each level out of a virtual address. */
static inline uint64_t pml4_index(uint64_t v) { return (v >> 39) & 0x1ff; }
static inline uint64_t pdpt_index(uint64_t v) { return (v >> 30) & 0x1ff; }
static inline uint64_t pd_index(uint64_t v)   { return (v >> 21) & 0x1ff; }
static inline uint64_t pt_index(uint64_t v)   { return (v >> 12) & 0x1ff; }

/* --- HHDM access ----------------------------------------------------------- */

/* Set once by paging_set_hhdm() before any walk. Lets us turn the physical
 * address inside an entry into a pointer we can actually dereference. */
static uint64_t hhdm_offset;

void paging_set_hhdm(uint64_t offset) {
    hhdm_offset = offset;
}

/* A page-table frame, addressed as 512 uint64_t entries, via the HHDM. */
static inline uint64_t *table_at(uint64_t table_phys) {
    return (uint64_t *)(table_phys + hhdm_offset);
}

/* --- The walk -------------------------------------------------------------- */

/* Return a pointer to the next-level table referenced by table[index].
 * If that entry is present, follow it. If it is absent and `create` is set,
 * allocate a fresh zeroed frame from pmm, install it (present + writable, plus
 * USER when the final mapping needs ring-3 reachability — a leaf is only user-
 * accessible if every table on the path to it is too), and return that.
 * Returns NULL only when a needed allocation fails, or when absent and !create. */
static uint64_t *next_level(uint64_t *table, uint64_t index,
                            bool create, uint64_t leaf_user_bit) {
    uint64_t entry = table[index];

    if (entry & PTE_PRESENT) {
        return table_at(entry & PTE_ADDR_MASK);
    }
    if (!create) {
        return NULL;
    }

    uint64_t frame = pmm_alloc_frame();
    if (frame == 0) {
        return NULL;
    }
    /* Zero it so all 512 entries start "not present" — a stray non-present
     * pattern here would otherwise be read as a bogus mapping. */
    memset(table_at(frame), 0, PMM_FRAME_SIZE);

    table[index] = (frame & PTE_ADDR_MASK) | PTE_PRESENT | PTE_WRITE | leaf_user_bit;
    return table_at(frame);
}

bool paging_map(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags) {
    /* Whether the final page (and therefore every table leading to it) must be
     * user-accessible. Propagated into the intermediate tables by next_level. */
    uint64_t leaf_user = (flags & VM_USER) ? PTE_USER : 0;

    uint64_t *pml4 = table_at(pml4_phys);
    uint64_t *pdpt = next_level(pml4, pml4_index(virt), true, leaf_user);
    if (!pdpt) { return false; }
    uint64_t *pd = next_level(pdpt, pdpt_index(virt), true, leaf_user);
    if (!pd) { return false; }
    uint64_t *pt = next_level(pd, pd_index(virt), true, leaf_user);
    if (!pt) { return false; }

    /* Write the leaf entry: physical frame + present + the requested caps. */
    uint64_t pte = (phys & PTE_ADDR_MASK) | PTE_PRESENT;
    if (flags & VM_WRITE) { pte |= PTE_WRITE; }
    if (flags & VM_USER)  { pte |= PTE_USER; }
    pt[pt_index(virt)] = pte;

    /* The CPU may hold a stale TLB entry for this VA (e.g. a prior not-present
     * one it cached). Flush just this page so the new mapping takes effect. */
    paging_invlpg(virt);
    return true;
}

bool paging_unmap(uint64_t pml4_phys, uint64_t virt) {
    uint64_t *pml4 = table_at(pml4_phys);
    uint64_t *pdpt = next_level(pml4, pml4_index(virt), false, 0);
    if (!pdpt) { return false; }
    uint64_t *pd = next_level(pdpt, pdpt_index(virt), false, 0);
    if (!pd) { return false; }
    uint64_t *pt = next_level(pd, pd_index(virt), false, 0);
    if (!pt) { return false; }

    uint64_t i = pt_index(virt);
    if (!(pt[i] & PTE_PRESENT)) {
        return false;
    }
    pt[i] = 0;
    paging_invlpg(virt);
    return true;
}

bool paging_translate(uint64_t pml4_phys, uint64_t virt, uint64_t *out_phys) {
    uint64_t *pml4 = table_at(pml4_phys);
    uint64_t *pdpt = next_level(pml4, pml4_index(virt), false, 0);
    if (!pdpt) { return false; }
    uint64_t *pd = next_level(pdpt, pdpt_index(virt), false, 0);
    if (!pd) { return false; }
    uint64_t *pt = next_level(pd, pd_index(virt), false, 0);
    if (!pt) { return false; }

    uint64_t entry = pt[pt_index(virt)];
    if (!(entry & PTE_PRESENT)) {
        return false;
    }
    if (out_phys) {
        /* Frame base from the entry, plus the original 12-bit page offset. */
        *out_phys = (entry & PTE_ADDR_MASK) | (virt & 0xfffull);
    }
    return true;
}

/* --- Control-register / TLB operations ------------------------------------- */

void paging_load_cr3(uint64_t pml4_phys) {
    /* "memory" clobber: tell the compiler memory may look different afterwards,
     * so it doesn't hoist loads/stores across the address-space change. */
    __asm__ volatile ("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

void paging_invlpg(uint64_t virt) {
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

uint64_t paging_read_cr2(void) {
    uint64_t cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    return cr2;
}

bool paging_can_switch_to(uint64_t pml4_phys) {
    /* The two addresses that MUST resolve in the new space or the cr3 load
     * triple-faults: the current stack pointer (the fault mechanism pushes to
     * it) and live kernel code (the next instruction is fetched through it). We
     * use a function in THIS file as a representative .text address. */
    uint64_t rsp;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp));
    uint64_t code = (uint64_t)(uintptr_t)&paging_load_cr3;

    uint64_t tmp;
    return paging_translate(pml4_phys, rsp, &tmp) &&
           paging_translate(pml4_phys, code, &tmp);
}
