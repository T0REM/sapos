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


## Notes

Sap OS is built with AI assistance (Claude) used as a learning tool. My original
goal was to do this to improve my knowledge in systems and low level programming.
Every subsystem is reviewed, debugged, and understood rather than blindly
generated. The AI plans and pair programs, leaving me to drive, test, and learn
how each piece actually works. Theres no point in me trying to claim credit for this,
its just to learn and understand.
