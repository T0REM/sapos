/* irq.h — hardware interrupt (IRQ) infrastructure, x86_64.
 *
 * The CPU-exception path (isr.c, vectors 0..31) and this hardware-IRQ path
 * (vectors 32..47) are deliberately separate: exceptions are faults in our own
 * code and are terminal for now, while IRQs are external device signals we
 * service and return from. They share only the asm frame layout.
 *
 * This is the arch seam for interrupts: drivers register a plain callback for
 * their IRQ line and never touch the IDT, the PIC, or the EOI themselves.
 */
#ifndef SAPOS_ARCH_X86_64_IRQ_H
#define SAPOS_ARCH_X86_64_IRQ_H

#include <stdint.h>

/* A device's IRQ handler. It does the device-specific work (read a port, bump a
 * counter) and returns. It must NOT send the EOI — the shared dispatch path
 * does that for every IRQ, so no driver can forget it and wedge the PIC. */
typedef void (*irq_handler_t)(void);

/* Install the 16 hardware-IRQ gates (vectors 32..47) into the IDT, pointing at
 * the asm stubs. Call after idt_init() and pic_remap(). Lines remain masked;
 * unmasking happens later, once handlers exist. */
void irq_init(void);

/* Register `handler` for hardware IRQ line `irq` (0..15). Replaces any previous
 * registration. Call before the line is unmasked. */
void irq_install_handler(uint8_t irq, irq_handler_t handler);

#endif /* SAPOS_ARCH_X86_64_IRQ_H */
