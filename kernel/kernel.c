/* kernel.c — Sap OS entry point (Phase 2).
 *
 * Phase 0 proved the pipeline (framebuffer + serial); Phase 1 added the x86_64
 * CPU tables (GDT, IDT, exception handlers, masked PIC). Phase 2 brings the
 * machine to life: it installs the PIT timer and PS/2 keyboard IRQ handlers,
 * unmasks those two lines, and enables interrupts. The kernel then idles,
 * waking on each interrupt instead of halting dead.
 *
 * This file is the core/entry layer, so it stays machine-agnostic: it drives
 * the arch layer through named seams (arch_init, arch_enable_irqs) and the
 * drivers through their init calls (ARCHITECTURE.md §4) rather than touching
 * GDT/IDT/PIC internals itself.
 *
 * No memory manager and no scheduler yet — the timer tick is the future basis
 * for scheduling, but that is Phase 4. Here it only counts.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "limine.h"
#include "lib/serial.h"
#include "lib/string.h"
#include "arch/x86_64/arch.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "core/mm/pmm.h"
#include "core/mm/vmm.h"
#include "core/mm/buddy.h"
#include "core/mm/slab.h"

/* --- Limine request block -------------------------------------------------
 *
 * Limine finds what we want by scanning our loaded image for tagged structures
 * (identified by magic numbers in limine.h). We place them all in the
 * .limine_requests* sections, which the linker script keeps and groups into
 * their own segment, bracketed by start/end markers so the scan is bounded.
 * 'used' stops the compiler from discarding them as unreferenced; 'volatile'
 * stops it assuming the fields never change (the bootloader writes them).
 */

/* Base revision marker: declares which revision of the Limine protocol we
 * speak. 6 is the current latest. The bootloader sets element [2] to 0 if it
 * supports it (checked via LIMINE_BASE_REVISION_SUPPORTED below). */
__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[3] = LIMINE_BASE_REVISION(6);

/* Ask for a framebuffer. The bootloader fills in `.response` before entry. */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
};

/* Ask for the physical memory map: which RAM ranges exist and what each is for
 * (usable, reserved, ACPI, kernel, ...). The frame allocator is built from this. */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
};

/* Ask for the HHDM (higher-half direct map) offset. Limine maps all physical RAM
 * at virtual address (phys + offset), which is how we get a writable pointer to
 * the bitmap, whose backing store we only know by physical address. */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
};

/* Ask where Limine loaded our kernel: the physical and virtual base of the
 * image. The VMM needs both to map the kernel into its own page tables — for
 * each kernel virtual page V, the backing frame is V - virtual_base +
 * physical_base. Without this we couldn't keep the executing code mapped across
 * the cr3 switch. */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_address_request executable_address_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
    .revision = 0,
};

/* Markers bracketing the request area for the bootloader's scanner. */
__attribute__((used, section(".limine_requests_start_marker")))
static volatile uint64_t limine_requests_start[4] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end_marker")))
static volatile uint64_t limine_requests_end[2] = LIMINE_REQUESTS_END_MARKER;

/* --- Halt ----------------------------------------------------------------- */

/* Halt forever. Mask interrupts (none are set up yet) then idle the CPU in a
 * low-power loop instead of spinning. 'noreturn' documents that we never come
 * back, so kmain need not handle a return. */
static __attribute__((noreturn)) void hcf(void) {
    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

/* --- Tiny serial number formatters ---------------------------------------
 *
 * Still no kprintf (that waits on the heap, Phase 3's later steps). These two
 * locals are all the memory summary and self-test need. */

/* Print an unsigned value in decimal. */
static void put_dec(uint64_t v) {
    if (v == 0) { serial_putc('0'); return; }
    char buf[20];
    int i = 0;
    while (v > 0) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i-- > 0) { serial_putc(buf[i]); }
}

/* Print a value as 0x-prefixed hex (handy for raw pointers / addresses). */
static void put_hex(uint64_t v) {
    serial_write("0x");
    char buf[16];
    int i = 0;
    do {
        uint8_t nib = v & 0xF;
        buf[i++] = (char)(nib < 10 ? '0' + nib : 'a' + (nib - 10));
        v >>= 4;
    } while (v > 0);
    while (i-- > 0) { serial_putc(buf[i]); }
}

/* Print the buddy allocator's per-order free-list counts plus total free MiB,
 * under a caller-supplied label. Kept here (not in buddy.c) so the core-layer
 * allocator stays free of the serial dependency, exactly like the pmm. */
static void print_buddy_stats(const char *label) {
    uint64_t counts[BUDDY_MAX_ORDER + 1];
    uint64_t total;
    buddy_get_stats(counts, &total);

    serial_write(label);
    serial_write("\n");
    for (unsigned o = 0; o <= BUDDY_MAX_ORDER; o++) {
        serial_write("    order ");
        put_dec(o);
        serial_write(" (");
        put_dec((PMM_FRAME_SIZE << o) / 1024);   /* KiB per block */
        serial_write(" KiB): ");
        put_dec(counts[o]);
        serial_write(" free\n");
    }
    serial_write("    total free: ");
    put_dec(total / (1024 * 1024));
    serial_write(" MiB\n");
}

/* Print the slab allocator's per-class accounting under a caller-supplied label.
 * Kept here (not in slab.c) so the core-layer allocator stays free of the serial
 * dependency, exactly like the pmm and buddy. */
static void print_slab_stats(const char *label) {
    struct slab_class_stats st[SLAB_NUM_CLASSES];
    slab_get_stats(st);

    serial_write(label);
    serial_write("\n");
    for (int i = 0; i < SLAB_NUM_CLASSES; i++) {
        serial_write("    class ");
        put_dec(st[i].obj_size);
        serial_write("B: slabs ");
        put_dec(st[i].slab_count);
        serial_write(", slots ");
        put_dec(st[i].total_slots);
        serial_write(" (");
        put_dec(st[i].free_slots);
        serial_write(" free), in use ");
        put_dec(st[i].bytes_in_use);
        serial_write(" B\n");
    }
}

/* --- Entry point ---------------------------------------------------------- */

void kmain(void) {
    /* Serial first, so we have a debug channel even if the framebuffer is
     * missing or the screen stays black. */
    serial_init();
    serial_write("Sap OS booted\n");

    /* Bail (loudly, over serial) if the bootloader couldn't honour our
     * protocol revision or didn't give us a framebuffer. */
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        serial_write("error: Limine base revision unsupported\n");
        hcf();
    }
    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count < 1) {
        serial_write("error: no framebuffer provided\n");
        hcf();
    }

    /* Use the first framebuffer. Limine guarantees a 32-bit-per-pixel mode by
     * default, so each pixel is one uint32_t laid out as 0x00RRGGBB. */
    struct limine_framebuffer *fb =
        framebuffer_request.response->framebuffers[0];

    const uint32_t colour = 0x00114422; /* solid sap green */

    /* Fill every pixel. We step rows by `pitch` (bytes per row, which may be
     * wider than width*4 due to padding) and index pixels within a row. */
    for (uint64_t y = 0; y < fb->height; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)fb->address + y * fb->pitch);
        for (uint64_t x = 0; x < fb->width; x++) {
            row[x] = colour;
        }
    }

    serial_write("Sap OS: framebuffer cleared\n");

    /* Bring up the x86_64 CPU tables (GDT, IDT, exception handlers, masked PIC,
     * and the hardware-IRQ gates). After this, exceptions are caught and dumped;
     * the IRQ path exists but every line is still masked and IF is still clear. */
    arch_init();
    serial_write("Sap OS: arch initialised (GDT, IDT, PIC, IRQs)\n");

    /* Install the device handlers BEFORE anything can fire. Order matters: if we
     * unmasked a line or ran `sti` first, an interrupt could arrive before its
     * handler existed and hit an empty gate. */
    timer_init(100);   /* PIT channel 0 at 100 Hz -> IRQ0 increments the tick */
    keyboard_init();   /* PS/2 keyboard            -> IRQ1 echoes keypresses  */
    serial_write("Sap OS: timer + keyboard handlers installed\n");

    /* Bring up the physical frame allocator (Phase 3, step 3a). We do this while
     * interrupts are still masked so the summary and self-test below print as one
     * clean block, without the timer heartbeat interleaving. Bail loudly if the
     * bootloader withheld either response we depend on. */
    if (memmap_request.response == NULL) {
        serial_write("error: no memory map provided\n");
        hcf();
    }
    if (hhdm_request.response == NULL) {
        serial_write("error: no HHDM offset provided\n");
        hcf();
    }
    pmm_init(memmap_request.response, hhdm_request.response->offset);

    /* Memory summary. usable frames * 4 KiB / 1 MiB gives MiB of usable RAM. */
    uint64_t total, used, free;
    pmm_get_stats(&total, &used, &free);
    serial_write("Sap OS: pmm up — usable ");
    put_dec(total * PMM_FRAME_SIZE / (1024 * 1024));
    serial_write(" MiB (");
    put_dec(total);
    serial_write(" frames), free ");
    put_dec(free);
    serial_write(" frames\n");

    /* Phase 3b: build our own page tables and switch onto them. We need the
     * kernel-address response (where Limine put the image) to map the kernel.
     * Still interrupts-masked here, so the cr3 switch happens with nothing able
     * to fire mid-flight. */
    if (executable_address_request.response == NULL) {
        serial_write("error: no kernel-address info provided\n");
        hcf();
    }
    vmm_init(executable_address_request.response,
             memmap_request.response,
             hhdm_request.response->offset);

    /* Phase 3c: the buddy allocator takes ownership of the bulk of usable RAM,
     * leaving the pmm a small reserve for its ongoing single-frame role (page
     * tables). After this, page-granularity allocation goes through the buddy;
     * the pmm and buddy own disjoint frames (see buddy.h / buddy_init). */
    buddy_init(memmap_request.response,
               hhdm_request.response->offset,
               BUDDY_PMM_RESERVE_FRAMES);
    serial_write("Sap OS: buddy up\n");
    print_buddy_stats("Sap OS: buddy free lists:");

    /* Phase 3d: the slab allocator turns buddy pages into a kmalloc/kfree pool of
     * small fixed-size objects. After this the memory subsystem is complete. It
     * borrows its backing pages from the buddy (never the pmm directly) and needs
     * the HHDM offset to turn those pages' physical addresses into pointers. */
    slab_init(hhdm_request.response->offset);
    serial_write("Sap OS: slab up\n");
    print_slab_stats("Sap OS: slab caches (empty):");

     /* Now go live: unmask IRQ0/IRQ1 and `sti`. Strictly after the handlers are
     * in place (see above), so the first interrupt lands somewhere real. */
    arch_enable_irqs();
    serial_write("Sap OS: interrupts enabled — idling, type to echo\n");

    /* Stay alive and responsive. `hlt` sleeps the CPU until the next interrupt;
     * the handler runs, control returns here, and we `hlt` again — sleeping
     * between interrupts rather than busy-spinning or halting dead. We do NOT
     * `cli` first (that is what hcf() does): IF must stay set so IRQs keep
     * waking us. */
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
