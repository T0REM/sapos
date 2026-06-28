/* pic.h — the legacy 8259A Programmable Interrupt Controller. */
#ifndef SCRAPOS_ARCH_X86_64_PIC_H
#define SCRAPOS_ARCH_X86_64_PIC_H

#include <stdint.h>

/* Remap the master/slave PICs so hardware IRQs 0..15 arrive on vectors 32..47
 * instead of their power-on default of 8..15 (which collide with CPU
 * exceptions), then MASK every IRQ. Phase 1 does not service hardware
 * interrupts — this is purely preparation so that Phase 2 can unmask lines as
 * it adds handlers. */
void pic_remap(void);

/* Send the End-Of-Interrupt for IRQ line `irq` (0..15). MUST be called at the
 * end of servicing every IRQ or the PIC stops delivering further interrupts.
 * Handles the cascade correctly: lines 8..15 come through the slave, so they
 * need an EOI to BOTH the slave and the master. */
void pic_send_eoi(uint8_t irq);

/* Clear the mask bit for IRQ line `irq` (0..15), letting that line through. */
void pic_unmask_irq(uint8_t irq);

#endif /* SCRAPOS_ARCH_X86_64_PIC_H */
