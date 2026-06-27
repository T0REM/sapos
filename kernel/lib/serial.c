/* serial.c — COM1 16550 UART driver.
 *
 * LAYERING: inb/outb are x86-specific port-I/O instructions. Per the
 * architecture doc's cardinal rule (§4), machine-specific primitives live in
 * the arch layer. As of Phase 1 that layer exists, so this driver pulls them
 * from kernel/arch/x86_64/io.h instead of defining its own (where they sat
 * temporarily in Phase 0).
 */
#include <stdint.h>
#include "serial.h"
#include "arch/x86_64/io.h"

/* COM1's base I/O port. The 16550 UART exposes 8 consecutive registers from
 * here; we address them as COM1 + offset. */
#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00); /* Disable all UART interrupts (we poll). */
    outb(COM1 + 3, 0x80); /* Set DLAB=1 to access the baud divisor latch.   */
    outb(COM1 + 0, 0x01); /* Divisor low byte: 1 -> 115200 baud.            */
    outb(COM1 + 1, 0x00); /* Divisor high byte.                             */
    outb(COM1 + 3, 0x03); /* DLAB=0; 8 data bits, no parity, 1 stop bit.    */
    outb(COM1 + 2, 0xC7); /* Enable + clear FIFOs, 14-byte trigger level.   */
    outb(COM1 + 4, 0x0B); /* Assert RTS/DTR, enable OUT2 (IRQ line gate).   */
}

void serial_putc(char c) {
    /* Line Status Register bit 5 (0x20) = Transmitter Holding Register Empty.
     * Spin until the UART is ready to take the next byte, then send it. */
    while ((inb(COM1 + 5) & 0x20) == 0) {
        /* busy-wait */
    }
    outb(COM1, (uint8_t)c);
}

void serial_write(const char *s) {
    for (; *s != '\0'; s++) {
        if (*s == '\n') {
            serial_putc('\r'); /* Terminals expect CR before LF. */
        }
        serial_putc(*s);
    }
}
