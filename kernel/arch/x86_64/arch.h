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
 * (and fully masked) PIC. After this returns, CPU exceptions are caught and
 * dumped; hardware interrupts are remapped but masked. */
void arch_init(void);

#endif /* SAPOS_ARCH_X86_64_ARCH_H */
