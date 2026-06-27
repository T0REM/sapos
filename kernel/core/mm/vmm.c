/* vmm.c — builds and owns the kernel address space.
 *
 * The policy half of paging: it walks the memory map and the kernel image and
 * calls the arch primitives (paging_map, etc.) to populate a fresh set of page
 * tables, then switches the CPU onto them. The mechanics of the table walk and
 * the cr3 load live one layer down in arch/x86_64/paging.c.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "vmm.h"
#include "core/mm/pmm.h"
#include "lib/serial.h"
#include "lib/string.h"

/* Linker-provided bounds of the kernel image (see linker.ld). They are virtual
 * addresses; declaring them as arrays makes &symbol the address itself. The
 * [__kernel_start, __data_start) half is code/constants (mapped read-only); the
 * [__data_start, __kernel_end) half is data/bss (mapped writable). */
extern char __kernel_start[];
extern char __data_start[];
extern char __kernel_end[];

/* The kernel's address space: the physical address of its top-level table. The
 * single space Phase 3b manages; later phases add per-process spaces. */
static uint64_t kernel_pml4;

/* Fail-stop for unrecoverable VMM errors this early in boot — there is nothing
 * to recover to and no kprintf, so say why over serial and park the CPU. */
static __attribute__((noreturn)) void vmm_panic(const char *msg) {
    serial_write("vmm panic: ");
    serial_write(msg);
    serial_write("\n");
    for (;;) { __asm__ volatile ("cli; hlt"); }
}

/* Map a [base, base+length) physical range into the HHDM, one 4 KiB page at a
 * time, at virtual address (phys + hhdm_offset). Writable, supervisor-only. */
static void map_hhdm_range(uint64_t base, uint64_t length, uint64_t hhdm_offset) {
    /* Round the range out to whole frames so a region that isn't frame-aligned
     * still gets fully covered. */
    uint64_t start = base & ~(uint64_t)(PMM_FRAME_SIZE - 1);
    uint64_t end   = (base + length + PMM_FRAME_SIZE - 1) & ~(uint64_t)(PMM_FRAME_SIZE - 1);

    for (uint64_t p = start; p < end; p += PMM_FRAME_SIZE) {
        if (!vmm_map(p + hhdm_offset, p, VM_WRITE)) {
            vmm_panic("out of frames mapping the HHDM");
        }
    }
}

/* Map a [virt_start, virt_end) span of the kernel image to its physical pages,
 * using Limine's reported virtual_base/physical_base to convert each VA. */
static void map_kernel_range(uint64_t virt_start, uint64_t virt_end,
                             uint64_t virtual_base, uint64_t physical_base,
                             uint64_t flags) {
    for (uint64_t v = virt_start; v < virt_end; v += PMM_FRAME_SIZE) {
        uint64_t phys = (v - virtual_base) + physical_base;
        if (!vmm_map(v, phys, flags)) {
            vmm_panic("out of frames mapping the kernel image");
        }
    }
}

void vmm_init(struct limine_executable_address_response *kaddr,
              struct limine_memmap_response *memmap,
              uint64_t hhdm_offset) {
    /* The paging layer needs the HHDM offset to reach the page-table frames it
     * allocates. Set it before the first vmm_map. */
    paging_set_hhdm(hhdm_offset);

    /* Allocate and zero the top-level table. Zeroing makes all 512 PML4 entries
     * "not present" — we then fill only the slots we actually map. We reach the
     * frame through the HHDM, which Limine's current tables still provide. */
    kernel_pml4 = pmm_alloc_frame();
    if (kernel_pml4 == 0) {
        vmm_panic("no frame for the kernel PML4");
    }
    memset((void *)(kernel_pml4 + hhdm_offset), 0, PMM_FRAME_SIZE);

    /* 1) Mirror the HHDM. Mapping every region Limine reports at phys+offset
     *    keeps all physical RAM reachable after the switch AND — critically —
     *    covers the live boot stack (Limine's stack lives in physical RAM the
     *    HHDM spans, and we are entered with rsp pointing into it), the pmm
     *    bitmap, and every page-table frame we allocate (they all come from this
     *    same usable RAM). Without this, the cr3 load loses the stack and
     *    triple-faults instantly. */
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        map_hhdm_range(e->base, e->length, hhdm_offset);
    }

    /* 2) Map the kernel image at the higher-half addresses it is linked at. This
     *    is what keeps the EXECUTING code mapped across the switch: rip is a
     *    higher-half VA inside .text, and every kernel global (GDT, IDT, serial
     *    state) is in this image. Code/rodata go read-only, data/bss writable —
     *    correct enough W^X without touching the NX bit (see linker.ld). */
    uint64_t vbase = kaddr->virtual_base;
    uint64_t pbase = kaddr->physical_base;
    map_kernel_range((uint64_t)(uintptr_t)__kernel_start,
                     (uint64_t)(uintptr_t)__data_start,
                     vbase, pbase, 0);                 /* read-only: code + rodata */
    map_kernel_range((uint64_t)(uintptr_t)__data_start,
                     (uint64_t)(uintptr_t)__kernel_end,
                     vbase, pbase, VM_WRITE);           /* writable: data + bss    */

    /* 3) Belt-and-suspenders: confirm the new tables resolve the current stack
     *    and live code BEFORE switching. A failure here would otherwise be a
     *    silent triple fault and CPU reset; instead we report it and halt. */
    if (!paging_can_switch_to(kernel_pml4)) {
        vmm_panic("new tables don't map the live code/stack — refusing cr3 load");
    }

    /* 4) The dangerous instruction. From the next fetch onward we translate
     *    through our own tables. We survive only because steps 1-3 guarantee rip
     *    and rsp stay mapped across this exact moment. */
    paging_load_cr3(kernel_pml4);

    serial_write("Sap OS: now running on our own page tables\n");
}

bool vmm_map(uint64_t virt, uint64_t phys, uint64_t flags) {
    return paging_map(kernel_pml4, virt, phys, flags);
}

bool vmm_unmap(uint64_t virt) {
    return paging_unmap(kernel_pml4, virt);
}

bool vmm_translate(uint64_t virt, uint64_t *out_phys) {
    return paging_translate(kernel_pml4, virt, out_phys);
}
