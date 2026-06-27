# Sap OS — Architecture

> A custom x86_64 kernel and operating system, built from the ground up in C.
> Not a Linux skin, not a Unix clone. Its own thing.

This document is the source of truth for how Sap OS is structured and why.
Read it before adding anything. If a change fights the rules here, the change
is probably wrong, or the rules need a deliberate update. Either way, decide on
purpose, not by accident.

---

## 1. Philosophy

Sap OS exists to be built, not to compete or ship. That gives us freedom:
no users to support, no roadmap pressure, no backwards compatibility. We make
the calls we find interesting and we own every line.

Guiding principles:

- **Be original where it's interesting, conventional where originality only buys pain.**
  Our brand, our design, our personality. But we borrow proven primitives
  (POSIX-ish file descriptors, read/write/open semantics) later on, because
  reinventing those teaches us nothing and costs us our sanity.
- **Earn complexity.** Start simple, upgrade when the simple thing actually
  hurts. We build toward sophisticated subsystems, but bottom-up, in dependency
  order, never by skipping the layers underneath.
- **Understand every line.** Claude Code writes the code; we (Charlie as builder,
  Claude as manager) understand it. Code we can't debug is worthless. The
  understanding is the real artifact.

---

## 2. Kernel design

**Monolithic-ish.** Drivers and core services live in kernel space. We are not
building a microkernel. Microkernels are elegant but force you to build IPC and
message passing before you can do anything fun, and they are brutal to debug as
a first kernel. We go monolithic now. If we later want to push a subsystem out
into userspace, we can, having earned it.

**Architecture target:** x86_64 only, for now. Long mode, higher-half kernel.

**Boot:** we use the Limine bootloader. It hands us a 64-bit higher-half kernel
with a framebuffer and a memory map already prepared. We do not fight this and
we do not write our own bootloader (that's a later flex, not a starting task).

---

## 3. The layered architecture

Bottom to top. Each layer may only depend on the layers below it.

### Boot layer
Limine. Drops us into a 64-bit higher-half kernel with a framebuffer and the
physical memory map. Out of our hands by design.

### Arch layer  (`kernel/arch/x86_64/`)
The x86_64-specific guts. The dirty assembly. GDT, IDT, interrupt entry stubs,
context switching, page-table manipulation, CPU control. Everything here knows
it's on x86. Nothing above here is allowed to.

### Core layer  (`kernel/core/`)
The architecture-independent heart. This is where Sap OS's personality lives:
the memory managers, the heap, the scheduler, task and process management. The
scheduler must never know what CPU it's running on. It asks the arch layer to
switch context; it does not know how that happens.

### Subsystem layer  (`kernel/fs/`, `kernel/drivers/`, syscalls)
The stuff that turns a kernel into something usable. Virtual filesystem,
device drivers, the syscall interface.

### User layer  (later)
Ring 3, an init process, a shell, eventually user programs.

---

## 4. The cardinal rule

**The arch layer and the core layer stay separated. Always.**

The scheduler does not know it is on x86. The memory manager does not inline
assembly. When the core layer needs something machine-specific, it calls a
clean, named function exposed by the arch layer, and that's the only seam
between them.

Most hobby kernels rot for exactly one reason: everything reaches into
everything, and within weeks you can't change one thing without breaking five.
This rule is what keeps Sap OS extensible for months instead of collapsing in
week three. If we ever wanted a second architecture (ARM, RISC-V), only the
arch layer would change. We probably never will, but designing as if we might
is what keeps the seams honest.

---

## 5. Directory structure

```
sapos/
├── boot/                  Limine config and boot assets
├── kernel/
│   ├── arch/x86_64/        GDT, IDT, interrupts, paging asm, context switch
│   ├── core/
│   │   ├── mm/             memory: frame allocator, vmm, buddy, slab
│   │   └── sched/          scheduler, tasks
│   ├── drivers/            serial, keyboard, timer (later: pci, ahci, ps2)
│   ├── fs/                 vfs (later: fat32, etc.)
│   ├── lib/                kprintf, string ops, kernel-internal helpers
│   └── kernel.c            kernel entry point
├── linker.ld              higher-half link layout
├── Makefile               build, ISO creation, qemu run targets
├── docs/
│   └── ARCHITECTURE.md     this file
└── tools/                 scripts, helpers
```

---

## 6. Memory subsystem (the build order that matters)

The destination is a proper buddy + slab allocator, like grown-up kernels run.
But the build order is fixed by dependency. You cannot skip up the stack.

1. **Memory map** — parse what Limine hands us. What RAM exists, what's usable.
2. **Physical frame allocator** — the humble one (bitmap or freelist). Hands out
   raw 4 KiB physical frames. Unavoidable and unglamorous. Everything else eats
   from this.
3. **Paging / virtual memory manager** — set up our own page tables, map the
   kernel into the higher half, manage virtual address space.
4. **Buddy allocator** — page-granularity allocation with splitting and
   coalescing, sitting on top of the frame allocator.
5. **Slab allocator** — small-object allocation (`kmalloc`/`kfree`) with object
   pooling, sitting on top of the buddy allocator.

The slab is the last brick, not the first. Anything that tries to start at the
top allocates from nothing and corrupts instantly.

---

## 7. Tech stack

- **Language:** C (plus the unavoidable x86_64 assembly in the arch layer).
- **Compiler:** clang targeting `x86_64-elf` (no cross-compiler build needed),
  freestanding, no red zone, no SSE/MMX in kernel code. May switch to a built
  `x86_64-elf-gcc` later if we want it.
- **Assembler:** NASM for standalone asm.
- **Bootloader:** Limine.
- **Emulator:** QEMU (`qemu-system-x86_64`), serial wired to stdio for debugging.
- **Build:** Make, producing a kernel ELF, then a bootable ISO via xorriso.
- **Dev environment:** WSL2 (Ubuntu) on Windows. All work inside the Linux
  filesystem (`~/sapos`), never on `/mnt/c/`. WSLg provides the QEMU window.

---

## 8. Phase roadmap

Each phase is several focused Claude Code sessions plus real debugging. We do
not advance until the current phase actually works in QEMU.

- **Phase 0 — Boot.** Toolchain, Limine, linker script, Makefile. Clear the
  framebuffer to a solid colour and print over serial. Proves the whole pipeline.
- **Phase 1 — Arch tables.** GDT, IDT, exception handlers, PIC remap. Stop
  triple-faulting.
- **Phase 2 — Interrupts and time.** Timer (PIT/APIC), keyboard handler,
  interrupt-driven input.
- **Phase 3 — Memory.** Frame allocator, paging/VMM, then buddy, then slab.
- **Phase 4 — Multitasking.** Task structures, context switch, scheduler.
- **Phase 5 — Userspace.** Ring 3, syscalls, ELF loading, separate address spaces.
- **Phase 6 — Filesystem and shell.** VFS, a simple filesystem, a shell in userspace.
- **Phase 7+ — The fun layers.** Drivers, a real filesystem, and whatever else
  we decide is worth building. This is where the "own brand" character compounds.

---

## 9. Working agreement

- Charlie builds and runs everything; Claude manages, writes the prompts, reviews
  the output, and catches bad ideas before they compound.
- When something breaks (it will, constantly), the QEMU output or crash dump gets
  pasted back. That's how problems get solved.
- One phase at a time. No firing later prompts blind. A wrong decision in an early
  layer poisons everything above it, so we get each layer right before moving on.
