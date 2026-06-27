/* isr.h — exception frame layout + the common C handler entry. */
#ifndef SAPOS_ARCH_X86_64_ISR_H
#define SAPOS_ARCH_X86_64_ISR_H

#include <stdint.h>

/* The stack snapshot the asm stubs hand to the C handler. The field order MUST
 * match the push order in isr.asm, lowest address first:
 *   - rax..r15 are pushed by isr_common (rax last => lowest address => first).
 *   - vector and error_code are pushed by the per-vector stub.
 *   - rip..ss are pushed by the CPU itself when the exception is taken.
 * Packed to guarantee no padding sneaks between fields. */
struct interrupt_frame {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t vector;       /* which exception fired (0..31)        */
    uint64_t error_code;   /* CPU error code, or 0 if none         */
    uint64_t rip;          /* faulting instruction pointer         */
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;          /* stack pointer at the time of the fault */
    uint64_t ss;
} __attribute__((packed));

/* Install the exception gates (vectors 0..31) into the IDT. Call after
 * idt_init(). */
void isr_init(void);

/* The single C handler every exception funnels into. Called from isr_common in
 * isr.asm with a pointer to the frame above. */
void isr_handler(struct interrupt_frame *frame);

#endif /* SAPOS_ARCH_X86_64_ISR_H */
