/* idt.c — IDT storage, gate setup, and lidt. */
#include <stdint.h>
#include "idt.h"
#include "gdt.h"   /* for GDT_KERNEL_CODE_SELECTOR */

/* One IDT gate descriptor (16 bytes in long mode). The 64-bit handler offset is
 * split awkwardly across three non-adjacent fields — a legacy of how this grew
 * from the 16- and 32-bit formats. Packed so the layout is exactly as the CPU
 * expects, byte for byte. */
struct idt_entry {
    uint16_t offset_low;   /* handler address bits 0..15   */
    uint16_t selector;     /* code segment selector to load */
    uint8_t  ist;          /* bits 0..2: IST index (0 = use current stack) */
    uint8_t  type_attr;    /* gate type, DPL, present       */
    uint16_t offset_mid;   /* handler address bits 16..31  */
    uint32_t offset_high;  /* handler address bits 32..63  */
    uint32_t zero;         /* reserved, must be 0           */
} __attribute__((packed));

/* The pointer fed to lidt: limit (size - 1) then the 64-bit base. */
struct idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/* The table. As a static (it lands in .bss) it is zero-initialised, which
 * conveniently leaves every gate "not present" (type_attr = 0) until we install
 * it. So an unhandled vector simply has a not-present gate. */
static struct idt_entry idt[256];
static struct idtr idtr;

void idt_set_gate(uint8_t vector, void *handler, uint8_t flags) {
    uint64_t addr = (uint64_t)handler;
    struct idt_entry *e = &idt[vector];

    e->offset_low  = (uint16_t)(addr & 0xFFFF);
    e->selector    = GDT_KERNEL_CODE_SELECTOR;
    e->ist         = 0;
    e->type_attr   = flags;
    e->offset_mid  = (uint16_t)((addr >> 16) & 0xFFFF);
    e->offset_high = (uint32_t)(addr >> 32);
    e->zero        = 0;
}

void idt_init(void) {
    idtr.limit = (uint16_t)(sizeof(idt) - 1);
    idtr.base  = (uint64_t)&idt;

    /* Load the IDT register. Gates can be (and are) installed after this — lidt
     * just records where the table lives. "memory" stops the compiler reordering
     * across the load. */
    __asm__ volatile ("lidt %0" : : "m"(idtr) : "memory");
}
