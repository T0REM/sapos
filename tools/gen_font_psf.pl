#!/usr/bin/perl
# gen_font_psf.pl — produce kernel/drivers/font_psf.h from a public-domain font.
#
# WHY THIS EXISTS
#   The framebuffer console (kernel/drivers/console.c) needs a bitmap font baked
#   into the kernel image. We want a *real* PSF (PC Screen Font) so the console
#   parses a genuine font header (magic, glyph count, width, height, bytes per
#   glyph) at runtime instead of hard-coding those numbers.
#
#   Rather than vendor a binary .psf blob (and an objcopy/incbin build step the
#   toolchain may not have), this script bakes a PSF2 file into a C byte array.
#   The committed header is self-contained: the build never touches a font file.
#
# SOURCE FONT
#   dhepper/font8x8 (font8x8_basic.h): the classic 8x8 IBM PC BIOS ROM glyphs,
#   released into the public domain. 128 glyphs, U+0000..U+007F, one byte per
#   row, 8 rows per glyph. In that file bit 0 (LSB) is the LEFTMOST pixel.
#
# WHAT WE EMIT
#   A valid PSF2 file (32-byte header + 128*8 bytes of glyph data) wrapped in a
#   C array. We BIT-REVERSE every source byte so the emitted glyphs are MSB-first
#   (bit 7 = leftmost pixel), which is the conventional PSF bit order — that way
#   the console renders glyphs the standard way and any real off-the-shelf PSF of
#   the same geometry would render identically.
#
# USAGE
#   perl tools/gen_font_psf.pl path/to/font8x8_basic.h > kernel/drivers/font_psf.h
#
# Regenerate only when changing fonts; the output is committed.

use strict;
use warnings;

my $src = shift // die "usage: gen_font_psf.pl <font8x8_basic.h>\n";
open(my $fh, '<', $src) or die "open $src: $!\n";

# Collect every glyph: each source line looks like
#   { 0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00},   // U+0041 (A)
# The file lists them in code-point order starting at U+0000.
my @glyphs;
while (my $line = <$fh>) {
    if ($line =~ /\{\s*((?:0x[0-9A-Fa-f]+\s*,\s*){7}0x[0-9A-Fa-f]+)\s*\}/) {
        my @bytes = map { hex($_) } ($1 =~ /0x([0-9A-Fa-f]+)/g);
        push @glyphs, \@bytes;
    }
}
close($fh);

die "expected 128 glyphs, got " . scalar(@glyphs) . "\n" unless @glyphs == 128;

# Reverse the 8 bits of a byte: LSB-first source -> MSB-first PSF convention.
sub bitrev {
    my $b = shift;
    my $r = 0;
    $r |= (($b >> $_) & 1) << (7 - $_) for 0..7;
    return $r;
}

my $WIDTH    = 8;
my $HEIGHT   = 8;
my $CHARSIZE = $HEIGHT * int(($WIDTH + 7) / 8);   # bytes per glyph = 8
my $LENGTH   = scalar(@glyphs);                   # 128

# Build the raw PSF2 byte stream. All multi-byte fields are little-endian.
my @bytes;
sub u32le { my $v = shift; push @bytes, ($v & 0xFF, ($v>>8)&0xFF, ($v>>16)&0xFF, ($v>>24)&0xFF); }

push @bytes, (0x72, 0xb5, 0x4a, 0x86);  # PSF2 magic
u32le(0);          # version
u32le(32);         # header size (bytes)
u32le(0);          # flags (0 = no unicode table)
u32le($LENGTH);    # number of glyphs
u32le($CHARSIZE);  # bytes per glyph
u32le($HEIGHT);    # glyph height (pixels)
u32le($WIDTH);     # glyph width  (pixels)

for my $g (@glyphs) {
    push @bytes, bitrev($_) for @$g;
}

# Emit the C header.
my $total = scalar(@bytes);
print <<"HDR";
/* font_psf.h — GENERATED, do not edit by hand.
 *
 * A complete PSF2 (PC Screen Font v2) file embedded as a C byte array, produced
 * by tools/gen_font_psf.pl from the public-domain dhepper/font8x8 ROM font.
 *
 * Layout: a 32-byte PSF2 header (magic, version, header size, flags, glyph
 * count, bytes-per-glyph, height, width — all little-endian) followed by
 * $LENGTH glyphs of $CHARSIZE bytes each ($WIDTH x $HEIGHT, MSB-first rows).
 * console.c parses this header at runtime; nothing here is hard-coded there.
 */
#ifndef SCRAPOS_DRIVERS_FONT_PSF_H
#define SCRAPOS_DRIVERS_FONT_PSF_H

static const unsigned char font_psf[$total] = {
HDR

for (my $i = 0; $i < $total; $i += 12) {
    my $end = $i + 11 < $total - 1 ? $i + 11 : $total - 1;
    my @row = @bytes[$i .. $end];
    print "    " . join(", ", map { sprintf("0x%02x", $_) } @row) . ",\n";
}

print <<"FTR";
};

#endif /* SCRAPOS_DRIVERS_FONT_PSF_H */
FTR
