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
#include "arch/x86_64/arch.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"

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
