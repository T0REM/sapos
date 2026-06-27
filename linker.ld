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

    /* Symbols the VMM reads to map the kernel image into its own page tables
     * (Phase 3b). They are PLAIN virtual addresses the linker resolves; the VMM
     * converts each to a physical address via Limine's executable-address
     * response (phys = virt - virtual_base + physical_base). The boundary is
     * __data_start: everything below it (requests, text, rodata) is mapped
     * read-only, everything from it up (data, bss) read-write. This base is
     * 4 KiB-aligned, and every section start below is page-aligned too, so the
     * VMM can map whole pages with no partial-page edge cases. */
    __kernel_start = .;

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

    /* Start of the writable half of the image. The VMM maps [__data_start,
     * __kernel_end) with the writable bit set, and everything before it without
     * it — giving the kernel's code and constants W-protection (the meaningful
     * half of W^X) without needing the NX bit, which would require enabling
     * EFER.NXE first or risk a reserved-bit fault on the cr3 switch. */
    __data_start = .;

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

    /* One past the end of the kernel image, page-aligned so the VMM's map loop
     * covers every page that holds kernel code or data. */
    . = ALIGN(CONSTANT(MAXPAGESIZE));
    __kernel_end = .;

    /* Drop sections that are useless to a freestanding kernel and that some
     * toolchains emit anyway. .eh_frame is C++ unwinding metadata; .note.*
     * can confuse the loader. */
    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
    }
}
