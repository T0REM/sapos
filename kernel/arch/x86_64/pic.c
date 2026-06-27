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

    /* Mask ALL 16 IRQ lines (1 = masked). Phase 2 will clear individual bits as
     * it installs timer/keyboard handlers. */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}
