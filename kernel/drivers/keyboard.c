/* keyboard.c — PS/2 keyboard: scancode -> ASCII, echoed to screen + serial (IRQ1). */
#include <stdint.h>
#include "keyboard.h"
#include "arch/x86_64/io.h"    /* inb — reading the scancode is port I/O (arch) */
#include "arch/x86_64/irq.h"   /* irq_install_handler                           */
#include "lib/serial.h"
#include "drivers/console.h"   /* echo each keypress onto the framebuffer       */

/* The PS/2 controller's data port. IRQ1 means "a byte is waiting here". */
#define PS2_DATA 0x60

/* Scancode SET 1 (the PC/AT default the PS/2 controller hands us) -> ASCII, for
 * the press codes 0x00..0x7F. 0 means "nothing printable" (Esc, the modifiers,
 * function keys, etc.). Release codes are the same values with bit 7 set and are
 * filtered out before we index this table.
 *
 * TODO: this is the UNSHIFTED layout only. Shift/Ctrl/CapsLock are not tracked
 * yet, so letters are always lowercase and the number row never yields its
 * shifted symbols. That is plenty for "watch my keys echo"; real modifier state
 * is a small follow-up, not Phase 2's job. */
static const char scancode_ascii[128] = {
    0,    0,   '1', '2', '3', '4', '5', '6',   /* 0x00-0x07  (0x01 Esc)        */
    '7',  '8', '9', '0', '-', '=', '\b','\t',  /* 0x08-0x0F  (0x0E Bksp,0x0F Tab)*/
    'q',  'w', 'e', 'r', 't', 'y', 'u', 'i',   /* 0x10-0x17                    */
    'o',  'p', '[', ']', '\n', 0,  'a', 's',   /* 0x18-0x1F  (0x1C Enter,0x1D Ctrl)*/
    'd',  'f', 'g', 'h', 'j', 'k', 'l', ';',   /* 0x20-0x27                    */
    '\'', '`', 0,   '\\','z', 'x', 'c', 'v',   /* 0x28-0x2F  (0x2A LShift)     */
    'b',  'n', 'm', ',', '.', '/', 0,   '*',   /* 0x30-0x37  (0x36 RShift)     */
    0,    ' ', 0,                              /* 0x38 LAlt, 0x39 Space        */
    /* 0x3A..0x7F (CapsLock, F-keys, keypad, ...) left 0 — unhandled for now.  */
};

/* IRQ1 handler. The controller raised the line because a byte is ready at 0x60;
 * we MUST read it (even to discard it) or the controller won't deliver the next. */
static void keyboard_irq(void) {
    uint8_t scancode = inb(PS2_DATA);

    /* Bit 7 set => key RELEASE (a "break" code). Phase 2 acts on presses only,
     * so ignore releases entirely. */
    if (scancode & 0x80) {
        return;
    }

    char c = scancode_ascii[scancode];
    if (c != 0) {
        /* On screen: hand the raw character to the console. It interprets '\n',
         * '\b' (visually erasing), '\t' and wraps/scrolls itself, so no '\r'
         * fix-up is needed here — that's serial's quirk, not the console's. */
        console_putc(c);

        /* On serial (the headless/debug mirror): Enter gives a bare '\n', so
         * prepend '\r' or a raw terminal stair-steps. (serial_putc, unlike
         * serial_write, does no \n -> \r\n translation.) */
        if (c == '\n') {
            serial_putc('\r');
        }
        serial_putc(c);
    }
}

void keyboard_init(void) {
    irq_install_handler(1, keyboard_irq);
}
