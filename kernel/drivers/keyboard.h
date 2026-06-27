/* keyboard.h — PS/2 keyboard driver (IRQ1).
 *
 * Like the timer, a legacy x86 device: it lives in kernel/drivers/ and depends
 * downward on the arch layer (io.h for port 0x60, irq.h to register itself).
 */
#ifndef SAPOS_DRIVERS_KEYBOARD_H
#define SAPOS_DRIVERS_KEYBOARD_H

/* Install the IRQ1 handler. On each key PRESS it reads the scancode, translates
 * it through a US QWERTY table, and echoes the character over serial. Does NOT
 * unmask IRQ1 — arch_enable_irqs() does that once all handlers are installed. */
void keyboard_init(void);

#endif /* SAPOS_DRIVERS_KEYBOARD_H */
