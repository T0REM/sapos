/* paging.h — x86_64 4-level paging primitives (Phase 3b).
 *
 * The dirty machine-specific half of virtual memory: the PML4 -> PDPT -> PD ->
 * PT walk, the cr3 load, invlpg, and reading cr2. Per ARCHITECTURE.md §4 this is
 * the ONLY place that knows about page-table bit layouts and control registers.
 * It publishes a deliberately machine-NEUTRAL interface — map a virtual page to
 * a physical frame, unmap, translate — that the core VMM (kernel/core/mm/vmm.c)
 * calls without ever seeing an x86 detail. The core layer passes the neutral
 * VM_* flags below; this layer alone turns them into hardware PTE bits.
 *
 * An "address space" here is just the physical address of its top-level table
 * (the PML4 frame). We pass it around as a uint64_t so the core layer needs no
 * x86 struct — it gets one opaque handle from the VMM and hands it back.
 */
#ifndef SCRAPOS_ARCH_X86_64_PAGING_H
#define SCRAPOS_ARCH_X86_64_PAGING_H

#include <stdint.h>
#include <stdbool.h>

/* Neutral capability flags the core layer requests for a mapping. "Present" is
 * implied by the act of mapping, so it is not a flag here; these are the extra
 * capabilities on top. paging.c maps each to the corresponding x86 PTE bit. */
#define VM_WRITE 0x1u   /* page is writable                         */
#define VM_USER  0x2u   /* page is reachable from ring 3 (user mode) */

/* Tell the paging layer the HHDM offset, so it can reach any page-table frame
 * (which it only knows by physical address) through a virtual pointer. Must be
 * called once before any map/unmap/translate. */
void paging_set_hhdm(uint64_t offset);

/* Map a single 4 KiB virtual page to a physical frame in the given address
 * space, creating any missing PDPT/PD/PT tables (pulled from pmm and zeroed).
 * `flags` is a bitwise-OR of VM_*; the present bit is always set. Flushes the
 * TLB entry for `virt`. Returns false only if an intermediate table was needed
 * and pmm was out of frames. `virt` and `phys` must be PMM_FRAME_SIZE-aligned. */
bool paging_map(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags);

/* Remove the mapping for a single page and flush its TLB entry. Returns false
 * if the page was not mapped. Intermediate tables are left in place (cheap, and
 * they get reused); reclaiming them is a later concern. */
bool paging_unmap(uint64_t pml4_phys, uint64_t virt);

/* Resolve `virt` to its backing physical address in the given space. On a hit,
 * writes the full physical address (frame base + page offset) to *out_phys and
 * returns true; returns false if any level along the walk is not present. */
bool paging_translate(uint64_t pml4_phys, uint64_t virt, uint64_t *out_phys);

/* Load an address space into cr3, making it the live translation. This is the
 * dangerous instruction (see vmm.c): the next fetch already uses the new tables.
 * Callers must verify the live code/stack are mapped first — see below. */
void paging_load_cr3(uint64_t pml4_phys);

/* Invalidate one page's TLB entry after changing its mapping. */
void paging_invlpg(uint64_t virt);

/* Read cr2, which holds the faulting linear address after a #PF. */
uint64_t paging_read_cr2(void);

/* Safety gate for the cr3 switch: confirm the given (not-yet-loaded) address
 * space maps BOTH the current stack pointer and this layer's own code. If it
 * returns false, loading cr3 would instantly triple-fault, so the caller must
 * report and halt instead of switching. Reading rsp is an x86 op, which is why
 * this check lives here in the arch layer rather than in the core VMM. */
bool paging_can_switch_to(uint64_t pml4_phys);

#endif /* SCRAPOS_ARCH_X86_64_PAGING_H */
