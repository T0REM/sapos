/* gdt.h — the Global Descriptor Table (x86_64).
 *
 * In 64-bit mode segmentation is mostly vestigial: the CPU ignores segment
 * base/limit for code and data. But we still must hand it a valid GDT with a
 * 64-bit code segment, because the CS selector's descriptor is what tells the
 * CPU "run in long mode, ring 0". So this is a minimal flat GDT.
 */
#ifndef SAPOS_ARCH_X86_64_GDT_H
#define SAPOS_ARCH_X86_64_GDT_H

/* Segment selectors = byte offset of the descriptor in the table. With the
 * null descriptor at index 0, kernel code is at index 1 (offset 0x08) and
 * kernel data at index 2 (offset 0x10). The low 3 bits (RPL/TI) are 0: ring 0,
 * GDT (not LDT). These selectors are what we load into CS / the data segs and
 * what the IDT gates reference. */
#define GDT_KERNEL_CODE_SELECTOR 0x08
#define GDT_KERNEL_DATA_SELECTOR 0x10

/* Build the GDT, load it with lgdt, and reload every segment register. */
void gdt_init(void);

#endif /* SAPOS_ARCH_X86_64_GDT_H */
