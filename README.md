# Sap OS

A custom x86_64 operating system kernel, written from scratch in C.
Not based on Linux. Its own thing, built phase by phase as a learning project.

## Status
- Phase 0: Boots via Limine, framebuffer + serial output
- Phase 1: GDT, IDT, CPU exception handlers, PIC remap
- Phase 2: PIT timer and PS/2 keyboard, live interrupt handling

## Building
Developed on WSL2 (Ubuntu). Needs clang, lld, nasm, xorriso, qemu, make.

    make        # build the bootable ISO
    make run    # boot it in QEMU

See docs/ARCHITECTURE.md for the design.
