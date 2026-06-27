/* arch.c — orders the x86_64 bring-up steps behind one clean call. */
#include "arch.h"
#include "gdt.h"
#include "idt.h"
#include "isr.h"
#include "pic.h"

void arch_init(void) {
    gdt_init();   /* 1. valid 64-bit segments (CS/data) before anything else.  */
    idt_init();   /* 2. install + load an (empty) IDT.                         */
    isr_init();   /* 3. fill vectors 0..31 with the exception handlers.        */
    pic_remap();  /* 4. move hardware IRQs to 32..47 and mask them all.        */
}
