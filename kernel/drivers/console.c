/* console.c — framebuffer text console. See console.h for the layering rationale.
 *
 * THE THREE IDEAS, up front (the rest is bookkeeping):
 *
 * 1. DRAWING A GLYPH. The font is a bitmap: one bit per pixel. A glyph is
 *    `glyph_h` rows; each row is `gw_bytes` bytes, MSB-first (bit 7 = leftmost
 *    pixel). To paint glyph `ch` at text cell (col,row) we walk every pixel of
 *    the cell: the cell's top-left pixel is (col*glyph_w, row*glyph_h); for each
 *    pixel we test the matching bit in the glyph and write the FOREGROUND colour
 *    if set, BACKGROUND if clear. A pixel lands in the framebuffer at byte offset
 *    y*pitch + x*bytes_per_pixel — pitch, not width*bpp, because the scanline may
 *    be padded wider than the visible pixels (see SCROLL).
 *
 * 2. CURSOR ADVANCE / WRAP. After a printable glyph the cursor moves one cell
 *    right. If that pushes it past the last column it wraps: column 0 of the next
 *    row. If THAT pushes it past the last row, we scroll instead of running off
 *    the bottom, and keep the cursor on the (now-blank) last row.
 *
 * 3. SCROLL. When we need a new line below the bottom one, we shift the whole
 *    picture up by exactly one text row (glyph_h pixel rows) and blank the new
 *    bottom row. One text row is glyph_h scanlines; a scanline is `pitch` bytes.
 *    So "shift up one text row" is a single memmove of (height - glyph_h)*pitch
 *    bytes from base+glyph_h*pitch down to base. We use memmove (not memcpy)
 *    because source and destination overlap. The row stride MUST be `pitch`, not
 *    width*4: the framebuffer's scanlines can be padded (pitch > width*bpp/8), so
 *    advancing by width*4 would creep diagonally and shear the image.
 */
#include <stdint.h>
#include <stdbool.h>
#include "console.h"
#include "lib/string.h"
#include "lib/serial.h"
#include "drivers/font_psf.h"   /* the embedded PSF2 font (generated) */

/* PSF2 (PC Screen Font v2) on-disk header. All fields little-endian; x86_64 is
 * little-endian so we read them straight out of the embedded byte array. */
struct psf2_header {
    uint32_t magic;       /* 0x864ab572 */
    uint32_t version;     /* 0 */
    uint32_t headersize;  /* offset to the first glyph (bytes) */
    uint32_t flags;       /* bit 0 = has unicode table (we don't use it) */
    uint32_t length;      /* number of glyphs */
    uint32_t charsize;    /* bytes per glyph */
    uint32_t height;      /* glyph height in pixels */
    uint32_t width;       /* glyph width in pixels */
};
#define PSF2_MAGIC 0x864ab572u

/* --- Framebuffer + font state (set once by console_init) ------------------- */
static uint8_t  *fb_base;        /* linear framebuffer, byte-addressed         */
static uint32_t  fb_width;       /* visible pixels per row                     */
static uint32_t  fb_height;      /* visible pixel rows                         */
static uint32_t  fb_pitch;       /* BYTES per scanline (>= fb_width*bytes_pp)  */
static uint32_t  fb_bytes_pp;    /* bytes per pixel (4 at 32bpp)               */

static const uint8_t *glyph_data; /* first glyph byte                          */
static uint32_t glyph_count;      /* glyphs available                          */
static uint32_t glyph_w, glyph_h; /* glyph size in pixels                      */
static uint32_t glyph_bytes;      /* bytes per glyph (== charsize)             */
static uint32_t gw_bytes;         /* bytes per glyph ROW = (glyph_w+7)/8       */

static uint32_t cols, rows;       /* text grid size                           */
static uint32_t cur_col, cur_row; /* cursor position, in cells                */

/* Colours in Limine's default 0x00RRGGBB layout. */
static const uint32_t fg = 0x00c8f0c8;  /* light sap green */
static const uint32_t bg = 0x00060f0a;  /* near-black green */

static bool ready;               /* false until a valid font is parsed         */

/* --- Pixel-level helpers --------------------------------------------------- */

/* Write one pixel. Offset = y*pitch + x*bytes_pp; we assume 32bpp so the store
 * is a single uint32_t. */
static inline void put_pixel(uint32_t x, uint32_t y, uint32_t colour) {
    *(uint32_t *)(fb_base + (uint64_t)y * fb_pitch + (uint64_t)x * fb_bytes_pp) = colour;
}

/* Paint text cell (col,row) with glyph `ch` (idea 1 above). Glyphs we don't have
 * are drawn as a blank cell. */
static void draw_cell(unsigned char ch, uint32_t col, uint32_t row) {
    if (ch >= glyph_count) ch = 0;            /* missing glyph -> blank        */
    const uint8_t *g = glyph_data + (uint32_t)ch * glyph_bytes;
    uint32_t px0 = col * glyph_w;
    uint32_t py0 = row * glyph_h;
    for (uint32_t r = 0; r < glyph_h; r++) {
        for (uint32_t cx = 0; cx < glyph_w; cx++) {
            /* MSB-first: the leftmost pixel is bit 7 of the row's first byte. */
            uint8_t byte = g[r * gw_bytes + (cx >> 3)];
            uint32_t bit = (byte >> (7 - (cx & 7))) & 1u;
            put_pixel(px0 + cx, py0 + r, bit ? fg : bg);
        }
    }
}

/* Clearing a cell is just drawing glyph 0 (a blank). */
static inline void clear_cell(uint32_t col, uint32_t row) { draw_cell(0, col, row); }

/* Underline cursor: bottom pixel row of the current cell, in fg. Erase redraws
 * it in bg. The cursor cell is normally empty, so touching only its bottom row
 * never eats a real glyph. */
static void paint_cursor(uint32_t colour) {
    uint32_t y = cur_row * glyph_h + (glyph_h - 1);
    uint32_t x0 = cur_col * glyph_w;
    for (uint32_t x = 0; x < glyph_w; x++) put_pixel(x0 + x, y, colour);
}
static inline void draw_cursor(void)  { paint_cursor(fg); }
static inline void erase_cursor(void) { paint_cursor(bg); }

/* --- Scrolling (idea 3 above) ---------------------------------------------- */
static void scroll(void) {
    uint32_t row_px = glyph_h;                          /* one text row in pixels */
    uint64_t move_bytes = (uint64_t)(fb_height - row_px) * fb_pitch;
    /* Shift everything up one text row. dest < src and the ranges overlap, so
     * memmove is required; memcpy would be undefined here. */
    memmove(fb_base, fb_base + (uint64_t)row_px * fb_pitch, move_bytes);
    /* Blank the freed bottom text row. We only touch visible pixels (0..fb_width);
     * any scanline padding past that stays as-is and is off-screen anyway. */
    for (uint32_t y = (rows - 1) * glyph_h; y < fb_height; y++)
        for (uint32_t x = 0; x < fb_width; x++)
            put_pixel(x, y, bg);
}

/* Move to the start of the next line, scrolling if we fall off the bottom. */
static void newline(void) {
    cur_col = 0;
    if (++cur_row >= rows) {
        scroll();
        cur_row = rows - 1;
    }
}

/* --- Public API ------------------------------------------------------------ */

void console_init(void *base, uint32_t width, uint32_t height,
                  uint32_t pitch, uint16_t bpp) {
    /* Parse the embedded PSF2 header. If the magic is wrong the font array is
     * corrupt/misgenerated; leave the console un-ready and shout over serial so
     * the headless path still tells us why the screen stayed blank. */
    const struct psf2_header *h = (const struct psf2_header *)font_psf;
    if (h->magic != PSF2_MAGIC || h->width == 0 || h->height == 0) {
        serial_write("console: bad PSF2 font magic/geometry — console disabled\n");
        ready = false;
        return;
    }

    fb_base     = (uint8_t *)base;
    fb_width    = width;
    fb_height   = height;
    fb_pitch    = pitch;
    fb_bytes_pp = bpp / 8;          /* 32bpp -> 4 */

    glyph_data  = font_psf + h->headersize;
    glyph_count = h->length;
    glyph_w     = h->width;
    glyph_h     = h->height;
    glyph_bytes = h->charsize;
    gw_bytes    = (glyph_w + 7) / 8;

    cols = fb_width  / glyph_w;
    rows = fb_height / glyph_h;
    cur_col = cur_row = 0;

    /* Clear the whole framebuffer to bg (idea: every visible pixel), then show
     * the cursor. */
    for (uint32_t y = 0; y < fb_height; y++)
        for (uint32_t x = 0; x < fb_width; x++)
            put_pixel(x, y, bg);

    ready = true;
    draw_cursor();
}

void console_putc(char c) {
    if (!ready) return;

    erase_cursor();    /* take the cursor down before we move/scroll/draw */

    switch (c) {
    case '\n':
        newline();
        break;
    case '\r':
        cur_col = 0;
        break;
    case '\b':
        /* Step back one cell (across the line boundary if needed) and wipe it. */
        if (cur_col > 0) {
            cur_col--;
        } else if (cur_row > 0) {
            cur_row--;
            cur_col = cols - 1;
        }
        clear_cell(cur_col, cur_row);
        break;
    case '\t': {
        /* Advance to the next 8-column tab stop, blanking the cells passed. */
        uint32_t target = (cur_col & ~7u) + 8;
        while (cur_col < target) {
            clear_cell(cur_col, cur_row);
            if (++cur_col >= cols) { newline(); break; }
        }
        break;
    }
    default:
        /* Printable glyphs only; other control bytes are ignored on screen. */
        if ((unsigned char)c >= 0x20) {
            draw_cell((unsigned char)c, cur_col, cur_row);
            if (++cur_col >= cols) newline();
        }
        break;
    }

    draw_cursor();     /* and bring it back at the new position */
}

void console_write(const char *s) {
    for (; *s != '\0'; s++) console_putc(*s);
}
