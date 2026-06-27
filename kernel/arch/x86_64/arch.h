/* arch.h — the single entry point the kernel uses to bring up x86_64.
 *
 * Per ARCHITECTURE.md §4, the core/entry layer must not poke arch internals
 * (gdt_init, idt_init, ...) one by one. It calls this one named function and
 * lets the arch layer decide the order. That keeps the seam between core and
 * arch narrow and honest.
 */
#ifndef SAPOS_ARCH_X86_64_ARCH_H
#define SAPOS_ARCH_X86_64_ARCH_H

/* Set up the CPU tables: GDT, then IDT + exception handlers, then the remapped
 * (and fully masked) PIC, then the hardware-IRQ gates. After this returns, CPU
 * exceptions are caught and dumped; the IRQ machinery is ready but every line is
 * still masked and interrupts are still disabled. */
void arch_init(void);

/* Go live on interrupts: unmask IRQ0 (timer) and IRQ1 (keyboard), then `sti`.
 * Call this LAST, only after the timer and keyboard handlers are installed, so
 * the first interrupt lands in a real handler and not an empty gate. Unmasking a
 * line and clearing the interrupt flag is x86/PIC state, so it lives here in the
 * arch layer rather than in kmain. */
void arch_enable_irqs(void);

#endif /* SAPOS_ARCH_X86_64_ARCH_H */
