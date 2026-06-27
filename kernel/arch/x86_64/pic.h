/* pic.h — the legacy 8259A Programmable Interrupt Controller. */
#ifndef SAPOS_ARCH_X86_64_PIC_H
#define SAPOS_ARCH_X86_64_PIC_H

/* Remap the master/slave PICs so hardware IRQs 0..15 arrive on vectors 32..47
 * instead of their power-on default of 8..15 (which collide with CPU
 * exceptions), then MASK every IRQ. Phase 1 does not service hardware
 * interrupts — this is purely preparation so that Phase 2 can unmask lines as
 * it adds handlers. */
void pic_remap(void);

#endif /* SAPOS_ARCH_X86_64_PIC_H */
