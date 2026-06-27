/* arch.c — orders the x86_64 bring-up steps behind one clean call. */
#include <stdint.h>
#include "arch.h"
#include "gdt.h"
#include "idt.h"
#include "isr.h"
#include "irq.h"
#include "pic.h"

#define IA32_APIC_BASE 0x1B    /* MSR holding the local APIC base + enable bit */
#define APIC_BASE_ENABLE (1u << 11)

/* Make the legacy 8259 PIC the CPU's interrupt source.
 *
 * Limine/the firmware hands us the CPU with the LOCAL APIC already enabled
 * (IA32_APIC_BASE bit 11 = 1). An enabled LAPIC sits between the 8259 and the
 * core: unless its LINT0 entry is set to ExtINT ("virtual-wire" mode), it
 * silently swallows the PIC's INTR signal, so unmasked IRQs never reach the CPU
 * even with IF=1. That is the bug Phase 2 hit — the PIT and keyboard were armed
 * correctly but no interrupt could ever fire.
 *
 * Phase 2 is deliberately a PIC-only kernel (we are not driving the APIC yet),
 * so the minimal correct fix is to DISABLE the local APIC by clearing the enable
 * bit. With the LAPIC off, the core's INTR pin is wired straight to the 8259 and
 * unmasked IRQs are delivered. A later phase that actually wants the APIC (SMP,
 * the APIC timer) will replace this with proper APIC setup. */
static void lapic_disable(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(IA32_APIC_BASE));
    lo &= ~APIC_BASE_ENABLE;
    __asm__ volatile ("wrmsr" : : "c"(IA32_APIC_BASE), "a"(lo), "d"(hi));
}

void arch_init(void) {
    gdt_init();      /* 1. valid 64-bit segments (CS/data) before anything else. */
    idt_init();      /* 2. install + load an (empty) IDT.                        */
    isr_init();      /* 3. fill vectors 0..31 with the exception handlers.       */
    lapic_disable(); /* 4. turn off the LAPIC so the PIC can reach the CPU.      */
    pic_remap();     /* 5. move hardware IRQs to 32..47 and mask them all.       */
    irq_init();      /* 6. fill vectors 32..47 with the hardware-IRQ stubs.      */
}

void arch_enable_irqs(void) {
    pic_unmask_irq(0);            /* IRQ0 — PIT timer                          */
    pic_unmask_irq(1);            /* IRQ1 — PS/2 keyboard                      */
    __asm__ volatile ("sti");    /* set IF: the CPU now accepts maskable IRQs */
}
