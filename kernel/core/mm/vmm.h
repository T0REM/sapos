/* vmm.h — the virtual memory manager (ARCHITECTURE.md §6, step 3).
 *
 * The architecture-neutral layer that owns the kernel's address space. It
 * decides WHAT to map (the HHDM, the kernel image) and drives the x86 page-table
 * primitives in kernel/arch/x86_64/paging.c to do it (§4: core calls a named
 * arch seam, never inline assembly). There is no x86 in this file.
 *
 * Phase 3b builds exactly one address space — the kernel's — and switches the
 * CPU onto it. The buddy allocator (3c) and slab (3d) come later and sit above
 * this; nothing here knows about them.
 */
#ifndef SCRAPOS_CORE_MM_VMM_H
#define SCRAPOS_CORE_MM_VMM_H

#include <stdint.h>
#include <stdbool.h>

#include "limine.h"
/* The neutral mapping flags (VM_WRITE, VM_USER) and the map/translate contract
 * live in the arch interface header; the VMM forwards straight to them. */
#include "arch/x86_64/paging.h"

/* Build the kernel's own page tables and switch the CPU onto them. Allocates a
 * fresh PML4 from pmm, maps every region Limine reports into the HHDM (so all
 * physical memory stays reachable at phys + hhdm_offset), maps the kernel image
 * at its higher-half link addresses (read-only code/rodata, writable data/bss),
 * verifies the live code and stack are mapped, then loads cr3. Call once, after
 * pmm_init() and with interrupts still masked. Halts on any unrecoverable error.
 *   kaddr       — Limine's executable-address response (kernel phys/virt base).
 *   memmap      — Limine's memory map (the regions to mirror into the HHDM).
 *   hhdm_offset — the higher-half direct map offset. */
void vmm_init(struct limine_executable_address_response *kaddr,
              struct limine_memmap_response *memmap,
              uint64_t hhdm_offset);

/* Map / unmap / translate a single page in the kernel address space. Thin
 * neutral wrappers over the arch primitives, operating on the space vmm_init()
 * built. `flags` is a bitwise-OR of the VM_* flags from paging.h. */
bool vmm_map(uint64_t virt, uint64_t phys, uint64_t flags);
bool vmm_unmap(uint64_t virt);
bool vmm_translate(uint64_t virt, uint64_t *out_phys);

#endif /* SCRAPOS_CORE_MM_VMM_H */
