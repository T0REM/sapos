/* pic.c — remap the 8259A PICs off the CPU exception range, then mask them.
 *
 * Why this matters even though we mask everything: at power-on the master PIC
 * raises IRQs on vectors 8..15, which overlap the CPU exception vectors (#DF is
 * 8, #PF is 14, ...). If a stray IRQ ever fired we could not tell a real fault
 * from a keyboard tick. So we relocate the IRQs to 32..47 now, once, and keep
 * them masked until Phase 2 actually handles them.
 */
#include <stdint.h>
#include "pic.h"
#include "io.h"

/* Each PIC has a command port and a data port. */
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

/* Initialisation Command Words. */
#define ICW1_INIT 0x10   /* begin init sequence            */
#define ICW1_ICW4 0x01   /* we will send an ICW4           */
#define ICW4_8086 0x01   /* 8086/88 mode (not 8080)        */

/* Operation Command Word 2: the non-specific End-Of-Interrupt command. */
#define PIC_EOI   0x20

void pic_remap(void) {
    /* ICW1: start the init sequence on both chips. io_wait() gives the PIC a
     * moment to digest each write (needed on real hardware). */
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4); io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4); io_wait();

    /* ICW2: the vector offset for each chip's 8 IRQs. */
    outb(PIC1_DATA, 0x20); io_wait();   /* master IRQ0..7  -> vectors 32..39 */
    outb(PIC2_DATA, 0x28); io_wait();   /* slave  IRQ8..15 -> vectors 40..47 */

    /* ICW3: describe the master/slave wiring. */
    outb(PIC1_DATA, 0x04); io_wait();   /* master: a slave is on IRQ2 (bit 2) */
    outb(PIC2_DATA, 0x02); io_wait();   /* slave:  its cascade identity is 2   */

    /* ICW4: put both chips in 8086 mode. */
    outb(PIC1_DATA, ICW4_8086); io_wait();
    outb(PIC2_DATA, ICW4_8086); io_wait();

    /* Mask ALL 16 IRQ lines (1 = masked). Phase 2 clears individual bits via
     * pic_unmask_irq() once the matching handlers are installed. */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_send_eoi(uint8_t irq) {
    /* Lines 8..15 are serviced by the SLAVE, which is cascaded into the master
     * on IRQ2. The slave has its own in-service bit, so it needs its own EOI —
     * and because the cascade also marked the master busy, the master needs one
     * too. So: slave first (only for 8..15), then the master, always. */
    if (irq >= 8) {
        outb(PIC2_CMD, PIC_EOI);
    }
    outb(PIC1_CMD, PIC_EOI);
}

void pic_unmask_irq(uint8_t irq) {
    /* The mask lives in each chip's data port (the Interrupt Mask Register):
     * lines 0..7 on the master, 8..15 on the slave. A 0 bit lets the line
     * through. Read-modify-write so we touch only the one bit. */
    uint16_t port;
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    uint8_t mask = inb(port) & ~(uint8_t)(1u << irq);
    outb(port, mask);
}
