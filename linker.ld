/* linker.ld — higher-half link layout for the Sap OS kernel.
 *
 * Limine loads us as an ordinary ELF64 executable and jumps to our ELF entry
 * point. We tell the linker to lay the kernel out in the topmost 2 GiB of the
 * 64-bit address space (the "higher half"), starting at 0xffffffff80000000,
 * which is the conventional kernel base and what -mcmodel=kernel expects.
 */

OUTPUT_FORMAT(elf64-x86-64)

/* The ELF entry point. Limine reads e_entry and jumps here. */
ENTRY(kmain)

/* Program headers (segments). Limine maps each PT_LOAD segment for us.
 * We give the Limine requests their own segment so the bootloader can find
 * them, and split text/rodata/data so each can get correct page permissions. */
PHDRS
{
    limine_requests PT_LOAD;
    text            PT_LOAD;
    rodata          PT_LOAD;
    data            PT_LOAD;
}

SECTIONS
{
    /* Higher-half base. Everything below is laid out from here upward. */
    . = 0xffffffff80000000;

    /* The Limine request structures. The start/end markers bracket the area
     * the bootloader scans, with the actual requests in between. KEEP() stops
     * --gc-sections from discarding them since nothing in our code "uses" them
     * in a way the linker can see. */
    .limine_requests : {
        KEEP(*(.limine_requests_start_marker))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end_marker))
    } :limine_requests

    /* Page-align before each following segment so text/rodata/data land on
     * separate 4 KiB pages and can later get distinct permissions. */
    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .text : {
        *(.text .text.*)
    } :text

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .data : {
        *(.data .data.*)
    } :data

    /* .bss must be the LAST thing in the data segment. It occupies no space in
     * the file (NOBITS); putting it last avoids writing a huge run of zeros
     * into the ELF on disk. */
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /* Drop sections that are useless to a freestanding kernel and that some
     * toolchains emit anyway. .eh_frame is C++ unwinding metadata; .note.*
     * can confuse the loader. */
    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
    }
}
