/* serial.h — COM1 serial port (16550 UART) output.
 *
 * Phase 0's only debug channel. QEMU is launched with COM1 wired to stdio, so
 * anything we write here lands in the terminal that ran `make run`.
 */
#ifndef SAPOS_LIB_SERIAL_H
#define SAPOS_LIB_SERIAL_H

/* Initialise the COM1 UART (baud, line format, FIFO). Call once at boot. */
void serial_init(void);

/* Write a single byte to COM1, blocking until the UART can accept it. */
void serial_putc(char c);

/* Write a NUL-terminated string to COM1. '\n' is expanded to "\r\n" so the
 * output looks right in a normal terminal. */
void serial_write(const char *s);

#endif /* SAPOS_LIB_SERIAL_H */
