/* gdt.c — minimal 64-bit GDT: null, kernel code, kernel data. */
#include <stdint.h>
#include "gdt.h"

/* The table itself. Each entry is a classic 8-byte segment descriptor.
 *
 * We size it for exactly the three descriptors Phase 1 needs. Phase 5
 * (userspace) will grow this: a ring-3 code segment, a ring-3 data segment,
 * and a TSS descriptor — note the TSS descriptor is 16 bytes in long mode, so
 * it occupies TWO of these slots. When that time comes, enlarge this array and
 * add a gdt_set_tss() helper; nothing else here needs to change. We do NOT add
 * the TSS now (no userspace, no separate kernel stack to switch to yet). */
static uint64_t gdt[3];

/* The pointer we feed to lgdt: a 16-bit limit (table size - 1) followed by the
 * 64-bit base address. Packed so the two fields are adjacent with no padding. */
struct gdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct gdtr gdtr;

/* Assemble one 8-byte descriptor from its access and flags bytes. In 64-bit
 * code/data the base and limit are ignored by the CPU, so we fill them with the
 * conventional "flat" values (base 0, limit 0xFFFFF) purely so the descriptor
 * is well-formed; only `access` and `flags` actually matter.
 *
 *   access byte:  P | DPL(2) | S | type(4)
 *   flags nibble: G | D/B | L | AVL                          */
static uint64_t gdt_descriptor(uint8_t access, uint8_t flags) {
    uint64_t desc = 0;
    desc |= 0xFFFFULL;                          /* limit 15:0            */
    desc |= (uint64_t)access << 40;             /* access byte (bits 40-47) */
    desc |= 0xFULL << 48;                       /* limit 19:16           */
    desc |= (uint64_t)(flags & 0xF) << 52;      /* flags nibble (52-55)  */
    return desc;
}

/* Reload the segment registers after lgdt. This is the subtle part.
 *
 * lgdt only changes which table the CPU reads; the segment registers still hold
 * their OLD selectors until we reload them. Data segments (ds/es/ss/fs/gs) take
 * a plain mov. But CS cannot be set with mov — the only ways to change CS are a
 * far jump, far call, or far return. We use a far return (lretq): push the new
 * CS selector and a return address, then lretq pops both, atomically loading CS
 * and jumping to our label. That's the canonical way to "activate" a new code
 * segment. */
static void gdt_load(struct gdtr *ptr) {
    __asm__ volatile (
        "lgdt %0\n\t"                   /* point the CPU at our new table   */
        "pushq %1\n\t"                  /* push new CS selector (0x08)      */
        "leaq 1f(%%rip), %%rax\n\t"     /* compute address of label 1       */
        "pushq %%rax\n\t"               /* push it as the return address    */
        "lretq\n\t"                     /* far-return: loads CS, jumps to 1: */
        "1:\n\t"
        "mov %2, %%eax\n\t"             /* kernel data selector (0x10)      */
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%ss\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        :
        : "m"(*ptr),
          "i"(GDT_KERNEL_CODE_SELECTOR),
          "i"(GDT_KERNEL_DATA_SELECTOR)
        : "rax", "memory"
    );
}

void gdt_init(void) {
    gdt[0] = 0; /* Null descriptor — required; the CPU faults if selector 0 is
                 * ever used to access memory. */

    /* Kernel code: access 0x9A = P=1, DPL=0, S=1 (code/data), type=1010
     * (executable, readable). flags 0xA = G=1, L=1 (64-bit). The L bit is what
     * actually puts the CPU in long mode for ring 0. */
    gdt[1] = gdt_descriptor(0x9A, 0xA);

    /* Kernel data: access 0x92 = P=1, DPL=0, S=1, type=0010 (writable data).
     * L is meaningless for data, but G=1 keeps it tidy. */
    gdt[2] = gdt_descriptor(0x92, 0xA);

    gdtr.limit = (uint16_t)(sizeof(gdt) - 1);
    gdtr.base  = (uint64_t)&gdt;

    gdt_load(&gdtr);
}
