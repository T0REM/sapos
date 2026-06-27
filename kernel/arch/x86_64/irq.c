/* irq.c — the common hardware-IRQ dispatch path.
 *
 * Mirrors isr.c, but for vectors 32..47 instead of the CPU exceptions. The asm
 * stubs (irq_stub_0..15 in isr_stubs.asm) all funnel into irq_dispatch() with a
 * fully-built interrupt_frame. We look up the registered handler, run it, and —
 * crucially — send the PIC End-Of-Interrupt before returning.
 */
#include <stdint.h>
#include <stddef.h>
#include "irq.h"
#include "isr.h"   /* struct interrupt_frame (shared with the exception path) */
#include "idt.h"
#include "pic.h"

#define IRQ_COUNT       16   /* IRQ0..15                                  */
#define IRQ_VECTOR_BASE 32   /* IRQ0 lives at IDT vector 32 (post-remap)   */

/* The 16 stub addresses exported by isr_stubs.asm, for wiring the IDT. */
extern void *irq_stub_table[];

/* One C callback per IRQ line. Zeroed in .bss, so an unregistered line is NULL
 * and simply gets an EOI with no handler called. */
static irq_handler_t handlers[IRQ_COUNT];

void irq_init(void) {
    /* Wire vectors 32..47 to their asm stubs as interrupt gates (IF cleared on
     * entry, so an IRQ handler is never itself interrupted). */
    for (uint8_t i = 0; i < IRQ_COUNT; i++) {
        idt_set_gate(IRQ_VECTOR_BASE + i, irq_stub_table[i], IDT_GATE_INTERRUPT);
    }
}

void irq_install_handler(uint8_t irq, irq_handler_t handler) {
    if (irq < IRQ_COUNT) {
        handlers[irq] = handler;
    }
}

/* Entry from irq_common (asm). Runs the device handler, then sends the EOI. */
void irq_dispatch(struct interrupt_frame *f) {
    uint8_t irq = (uint8_t)(f->vector - IRQ_VECTOR_BASE);

    if (irq < IRQ_COUNT && handlers[irq] != NULL) {
        handlers[irq]();
    }

    /* Send the EOI unconditionally — even for an unhandled/spurious line. If we
     * skipped it, the PIC's in-service bit would stay set and it would never
     * deliver another interrupt on that line (or any lower-priority one). This
     * is the single most important line in the whole IRQ path. */
    pic_send_eoi(irq);
}
