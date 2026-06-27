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

## Milestones

**It's actually becoming a computer!**

Sap OS boots on its own page tables, manages memory through a full four layer
allocator, this is physical frames, paging, a buddy allocator, and a slab allocator with
kmalloc, kfree. It handles timer and keyboard interrupts, and renders text to its
own framebuffer console. You can type into it and watch the characters appear on
screen, with backspace and scrolling. Four phases of low level systems work, all written
from scratch in C.

![Sap OS booting and echoing keyboard input](screenshots/qemu-window.png)
## Notes

Sap OS is built with AI assistance (Claude) used as a learning tool. My original
goal was to do this to improve my knowledge in systems and low level programming.
Every subsystem is reviewed, debugged, and understood rather than blindly
generated. The AI plans and pair programs, leaving me to drive, test, and learn
how each piece actually works. Theres no point in me trying to claim credit for this,
its just to learn and understand.
