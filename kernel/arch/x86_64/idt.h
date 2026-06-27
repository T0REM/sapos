/* idt.h — the Interrupt Descriptor Table (x86_64).
 *
 * The IDT maps each of the 256 interrupt/exception vectors to a handler. When
 * the CPU takes vector N, it reads IDT[N], switches to that gate's code segment,
 * and jumps to its offset. We build the table and expose a clean way to install
 * one gate at a time.
 */
#ifndef SAPOS_ARCH_X86_64_IDT_H
#define SAPOS_ARCH_X86_64_IDT_H

#include <stdint.h>

/* Gate type/attribute byte. Both have P=1 (present) and DPL=0 (only ring 0 may
 * invoke them via software):
 *   0x8E = 64-bit INTERRUPT gate — clears IF on entry (no nested IRQs).
 *   0x8F = 64-bit TRAP gate      — leaves IF untouched.
 * We use interrupt gates for the CPU exceptions. */
#define IDT_GATE_INTERRUPT 0x8E
#define IDT_GATE_TRAP      0x8F

/* Zero the table and load it with lidt. Call once at boot. */
void idt_init(void);

/* Point vector `vector` at `handler`, with the given type/attr byte. The
 * handler's code segment is always the kernel code selector. */
void idt_set_gate(uint8_t vector, void *handler, uint8_t flags);

#endif /* SAPOS_ARCH_X86_64_IDT_H */
