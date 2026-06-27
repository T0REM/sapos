/* io.h — x86_64 port-mapped I/O primitives.
 *
 * These are the in/out instructions that talk to legacy device registers (the
 * UART, the PIC, the PIT, ...). They are pure x86, so per ARCHITECTURE.md §4
 * they live in the arch layer and nothing above arch/ uses them directly — a
 * driver includes this header, but the core layer never does.
 *
 * They were temporarily in kernel/lib/serial.c during Phase 0; this is their
 * permanent home. Kept header-only and `static inline` because port I/O is a
 * single instruction — inlining it avoids a function call per byte in tight
 * loops like the serial transmit poll.
 */
#ifndef SAPOS_ARCH_X86_64_IO_H
#define SAPOS_ARCH_X86_64_IO_H

#include <stdint.h>

/* Write one byte to an I/O port.
 *   "a"(value)  -> the byte must be in AL (outb only takes AL).
 *   "Nd"(port)  -> port in DX, or an 8-bit immediate if it fits (the 'N'). */
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

/* Read one byte from an I/O port. Result comes back in AL ("=a"). */
static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* Tiny I/O delay. Writing to unused port 0x80 burns roughly one bus cycle,
 * which old PICs need between back-to-back command writes. Harmless on QEMU,
 * correct on real hardware. */
static inline void io_wait(void) {
    __asm__ volatile ("outb %%al, $0x80" : : "a"((uint8_t)0));
}

#endif /* SAPOS_ARCH_X86_64_IO_H */
