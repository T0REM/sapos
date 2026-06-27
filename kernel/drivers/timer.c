/* timer.c — PIT channel 0 as a periodic tick source (IRQ0). */
#include <stdint.h>
#include "timer.h"
#include "arch/x86_64/io.h"    /* outb — talking to the PIT is port I/O (arch) */
#include "arch/x86_64/irq.h"   /* irq_install_handler                          */
#include "lib/serial.h"

/* PIT ports and the command byte. */
#define PIT_CH0_DATA 0x40
#define PIT_CMD      0x43
#define PIT_BASE_HZ  1193182u  /* the PIT's fixed input clock (~1.193 MHz) */

/* Command 0x36 = 0b0011_0110:
 *   bits 6-7 (00)  channel 0
 *   bits 4-5 (11)  access mode: lobyte then hibyte
 *   bits 1-3 (011) mode 3: square-wave generator (even, periodic ticks)
 *   bit  0   (0)   16-bit binary counter (not BCD) */
#define PIT_CMD_CH0_RATEGEN 0x36

/* volatile: the handler mutates this behind the main thread's back, so reads in
 * timer_ticks() must not be cached. */
static volatile uint64_t ticks;

/* Configured frequency, kept so the handler knows how many ticks make a second
 * for its once-per-second heartbeat print. */
static uint32_t tick_hz;

/* Print an unsigned value in decimal. We have no kprintf yet; this local helper
 * is all the heartbeat line needs. */
static void put_dec(uint64_t v) {
    if (v == 0) { serial_putc('0'); return; }
    char buf[20];
    int i = 0;
    while (v > 0) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i-- > 0) { serial_putc(buf[i]); }
}

/* IRQ0 handler. Runs ~`tick_hz` times a second, so it must stay tiny. */
static void timer_irq(void) {
    ticks++;

    /* Do NOT print every tick — at 100 Hz that would flood the serial log and
     * the blocking UART writes would themselves dominate the tick. Instead emit
     * one heartbeat line per second, so time is visibly advancing. */
    if (tick_hz != 0 && (ticks % tick_hz) == 0) {
        serial_write("[timer] tick ");
        put_dec(ticks / tick_hz);
        serial_write("s\n");
    }
}

void timer_init(uint32_t frequency) {
    tick_hz = frequency;

    /* divisor = input clock / desired frequency. 1193182 / 100 = 11932, which
     * fits the PIT's 16-bit counter. The PIT divides its input clock by this to
     * get the IRQ0 rate. */
    uint16_t divisor = (uint16_t)(PIT_BASE_HZ / frequency);

    outb(PIT_CMD, PIT_CMD_CH0_RATEGEN);
    outb(PIT_CH0_DATA, (uint8_t)(divisor & 0xFF));         /* low byte first  */
    outb(PIT_CH0_DATA, (uint8_t)((divisor >> 8) & 0xFF));  /* then high byte  */

    /* Register with the arch IRQ layer. The line is still masked at this point;
     * nothing fires until arch_enable_irqs(). */
    irq_install_handler(0, timer_irq);
}

uint64_t timer_ticks(void) {
    return ticks;
}
