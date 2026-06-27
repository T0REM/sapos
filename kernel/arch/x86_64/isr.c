/* isr.c — the crash screen.
 *
 * This is where every CPU exception ends up. In Phase 1 we have no way to
 * recover from a fault (no paging, no process to kill), so every exception is
 * terminal: we dump everything we know over serial and halt. Making this dump
 * readable matters — it is the only debugging window we will have when the
 * memory and scheduler phases start triple-faulting.
 */
#include <stdint.h>
#include "isr.h"
#include "idt.h"
#include "paging.h"        /* paging_read_cr2 — the faulting address on a #PF */
#include "lib/serial.h"

/* The 32 stub addresses exported by isr.asm, for wiring the IDT. */
extern void *isr_stub_table[];

/* Human-readable name per exception vector. The "#XX" mnemonics are the ones
 * the Intel SDM uses, so they are easy to look up. */
static const char *const exception_names[32] = {
    "#DE Divide Error",
    "#DB Debug",
    "NMI Non-Maskable Interrupt",
    "#BP Breakpoint",
    "#OF Overflow",
    "#BR BOUND Range Exceeded",
    "#UD Invalid Opcode",
    "#NM Device Not Available",
    "#DF Double Fault",
    "Coprocessor Segment Overrun",
    "#TS Invalid TSS",
    "#NP Segment Not Present",
    "#SS Stack-Segment Fault",
    "#GP General Protection Fault",
    "#PF Page Fault",
    "(reserved)",
    "#MF x87 FPU Floating-Point Error",
    "#AC Alignment Check",
    "#MC Machine Check",
    "#XM SIMD Floating-Point Exception",
    "#VE Virtualization Exception",
    "#CP Control Protection Exception",
    "(reserved)", "(reserved)", "(reserved)", "(reserved)",
    "(reserved)", "(reserved)", "(reserved)",
    "#VC VMM Communication Exception",
    "#SX Security Exception",
    "(reserved)",
};

/* Print a 64-bit value as a fixed-width 0x... hex string. We have no kprintf
 * yet (that is a later lib/ job); this local helper is all the crash dump
 * needs. Fixed width keeps the columns aligned and readable. */
static void put_hex64(uint64_t v) {
    static const char digits[] = "0123456789abcdef";
    char buf[16];
    for (int i = 15; i >= 0; i--) {
        buf[i] = digits[v & 0xF];
        v >>= 4;
    }
    serial_putc('0');
    serial_putc('x');
    for (int i = 0; i < 16; i++) {
        serial_putc(buf[i]);
    }
}

/* Print two "name 0xvalue" columns on one line, for a compact register dump. */
static void dump_pair(const char *na, uint64_t a, const char *nb, uint64_t b) {
    serial_write("  ");
    serial_write(na); serial_putc(' '); put_hex64(a);
    serial_write("   ");
    serial_write(nb); serial_putc(' '); put_hex64(b);
    serial_write("\n");
}

void isr_init(void) {
    /* Wire vectors 0..31 to their asm stubs as interrupt gates. Vectors 32..255
     * stay "not present" (the IDT is zeroed) until later phases install them. */
    for (uint8_t v = 0; v < 32; v++) {
        idt_set_gate(v, isr_stub_table[v], IDT_GATE_INTERRUPT);
    }
}

void isr_handler(struct interrupt_frame *f) {
    const char *name = (f->vector < 32) ? exception_names[f->vector]
                                        : "(unknown)";

    serial_write("\n");
    serial_write("================= CPU EXCEPTION =================\n");
    serial_write("  vector ");
    put_hex64(f->vector);
    serial_write("  ");
    serial_write(name);
    serial_write("\n");
    serial_write("  error  ");
    put_hex64(f->error_code);
    serial_write("\n");

    serial_write("  -- registers --\n");
    dump_pair("rax", f->rax, "rbx", f->rbx);
    dump_pair("rcx", f->rcx, "rdx", f->rdx);
    dump_pair("rsi", f->rsi, "rdi", f->rdi);
    dump_pair("rbp", f->rbp, "rsp", f->rsp);
    dump_pair("r8 ", f->r8,  "r9 ", f->r9);
    dump_pair("r10", f->r10, "r11", f->r11);
    dump_pair("r12", f->r12, "r13", f->r13);
    dump_pair("r14", f->r14, "r15", f->r15);

    serial_write("  -- fault frame --\n");
    dump_pair("rip", f->rip, "cs ", f->cs);
    dump_pair("rfl", f->rflags, "ss ", f->ss);

    /* For a page fault the faulting address lives in CR2, not in any saved
     * register, so read it specially, and decode the error code's bits. This is
     * our primary debugging tool for the whole memory phase: it turns what would
     * be a silent triple fault into a readable report of what access faulted,
     * where, and why. (Reading CR2 is x86, so the primitive lives in paging.c.)
     *
     * #PF error-code bits (Intel SDM):
     *   0 P   0 = page not present       1 = protection violation
     *   1 W   0 = read                    1 = write
     *   2 U   0 = supervisor (ring 0)     1 = user (ring 3)
     *   3 R   1 = reserved bit set in a paging-structure entry
     *   4 I   1 = instruction fetch */
    if (f->vector == 14) {
        uint64_t cr2 = paging_read_cr2();
        uint64_t ec  = f->error_code;

        serial_write("  -- page fault --\n");
        serial_write("  cr2 (faulting addr) ");
        put_hex64(cr2);
        serial_write("\n");
        serial_write("  cause: ");
        serial_write((ec & (1u << 0)) ? "protection-violation"
                                      : "page-not-present");
        serial_write(", ");
        serial_write((ec & (1u << 1)) ? "write" : "read");
        serial_write(", ");
        serial_write((ec & (1u << 2)) ? "user" : "supervisor");
        if (ec & (1u << 3)) { serial_write(", reserved-bit-set"); }
        if (ec & (1u << 4)) { serial_write(", instruction-fetch"); }
        serial_write("\n");
    }

    serial_write("  system halted.\n");
    serial_write("================================================\n");

    /* Nothing to recover to. Mask interrupts and park the CPU forever. */
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
