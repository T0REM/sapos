/* console.h — framebuffer text console (the on-screen output device).
 *
 * LAYERING (ARCHITECTURE.md §4/§5): this is a DEVICE DRIVER, so it lives in
 * kernel/drivers/, alongside the keyboard (the input device) and timer. It is
 * NOT core/: core is the architecture-independent algorithmic heart (mm, sched),
 * not hardware-facing services. It is NOT arch/: it contains no x86 assembly and
 * no port I/O — it only writes pixels into the linear framebuffer Limine handed
 * us, which is plain memory. The seam it depends on downward is just <stdint.h>
 * and string.h (memmove/memset); it never reaches up into the kernel.
 *
 * It is the screen twin of serial.c: serial stays the headless/debug channel,
 * this is the on-screen channel. A kprint helper in kernel.c fans status text
 * out to both.
 */
#ifndef SCRAPOS_DRIVERS_CONSOLE_H
#define SCRAPOS_DRIVERS_CONSOLE_H

#include <stdint.h>

/* Bring the console up over the Limine framebuffer. base/width/height/pitch/bpp
 * come straight from the framebuffer response (pitch is BYTES per scanline, bpp
 * is bits per pixel — we assume 32). The font is not a parameter: a PSF2 font is
 * compiled into the kernel (font_psf.h) and parsed here. Clears the screen to
 * the background colour and parks the cursor at (0,0). */
void console_init(void *base, uint32_t width, uint32_t height,
                  uint32_t pitch, uint16_t bpp);

/* Draw one character at the cursor and advance it. Handles '\n', '\r', '\b'
 * (erasing), and '\t'; wraps at the right edge and scrolls at the bottom. */
void console_putc(char c);

/* console_putc over a NUL-terminated string. */
void console_write(const char *s);

#endif /* SCRAPOS_DRIVERS_CONSOLE_H */
