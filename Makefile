# Makefile — Sap OS build pipeline (Phase 0).
#
# Flow:  *.c --clang--> *.o --ld.lld--> kernel.elf --xorriso--> sapos.iso --qemu
#
# Targets:
#   make            build the bootable ISO (default)
#   make run        build the ISO and boot it in QEMU (serial on stdio)
#   make clean      remove build outputs (keeps vendored Limine)
#   make distclean  also remove the vendored Limine + downloaded header

# ----------------------------------------------------------------------------
# Pinned versions (reproducible builds — bump deliberately, not by accident).
# ----------------------------------------------------------------------------
LIMINE_VERSION     := v12.3.3
# Commit of the limine-protocol repo that provides limine.h. Matched to the
# Limine version above.
LIMINE_PROTO_COMMIT := 80ef54bed402b8c0b672a707c1df4c532f3428ad

# ----------------------------------------------------------------------------
# Toolchain
# ----------------------------------------------------------------------------
CC := clang
LD := ld.lld
# NASM assembles the standalone .asm in the arch layer (the ISR stubs). Per
# ARCHITECTURE.md §7, NASM is our assembler for standalone asm.
AS := nasm

# Compiler flags. These encode the architecture doc's mandate: clang targeting
# x86_64-elf, freestanding, no red zone, no SSE/MMX, higher-half (kernel code
# model). Each flag, briefly:
#   -target x86_64-elf   bare-metal x86-64 ELF, no host OS assumptions
#   -ffreestanding       no hosted libc/startup; only the freestanding subset
#   -fno-stack-protector / -fno-stack-check   no canaries; we have no runtime
#   -fno-pie -fno-pic    fixed load address, not position-independent
#   -mno-red-zone        the SysV red zone is unsafe once we take interrupts
#   -mno-mmx -mno-sse -mno-sse2 -mno-80387   no FPU/vector regs in kernel code
#   -mcmodel=kernel      code/data live in the top 2GiB (the higher half)
#   -ffunction-sections -fdata-sections   let --gc-sections drop unused bits
CFLAGS := \
    -target x86_64-elf \
    -ffreestanding \
    -fno-stack-protector \
    -fno-stack-check \
    -fno-pie -fno-pic \
    -m64 -march=x86-64 -mabi=sysv \
    -mno-80387 -mno-mmx -mno-sse -mno-sse2 \
    -mno-red-zone \
    -mcmodel=kernel \
    -ffunction-sections -fdata-sections \
    -Wall -Wextra -std=gnu11 -O2 -g \
    -I kernel

# NASM flags: emit 64-bit ELF objects with DWARF debug info so addresses in a
# crash dump can be mapped back to source.
ASMFLAGS := -f elf64 -g -F dwarf

# Linker flags:
#   -m elf_x86_64        output an x86-64 ELF
#   -nostdlib -static    no host libraries, no dynamic linking
#   -z max-page-size=0x1000   align segments to 4KiB (not lld's default 2MiB),
#                             keeping the image small
#   --gc-sections        drop unreferenced sections (KEEP() protects requests)
#   --build-id=none      omit the build-id note (we discard .note anyway)
#   -T linker.ld         our higher-half layout
LDFLAGS := \
    -m elf_x86_64 \
    -nostdlib -static \
    -z max-page-size=0x1000 \
    --gc-sections \
    --build-id=none \
    -T linker.ld

# ----------------------------------------------------------------------------
# Sources / outputs
# ----------------------------------------------------------------------------
BUILD := build
KERNEL_ELF := $(BUILD)/kernel.elf
ISO := sapos.iso

# All kernel C sources. Add new files here as the kernel grows.
SRCS := \
    kernel/kernel.c \
    kernel/lib/serial.c \
    kernel/lib/string.c \
    kernel/arch/x86_64/gdt.c \
    kernel/arch/x86_64/idt.c \
    kernel/arch/x86_64/isr.c \
    kernel/arch/x86_64/pic.c \
    kernel/arch/x86_64/arch.c

# Standalone assembly sources (NASM). Named *_stubs to avoid colliding with the
# C object of the same stem (isr.c -> isr.o).
ASMSRCS := \
    kernel/arch/x86_64/isr_stubs.asm

# Mirror each source to build/<path>.o
OBJS := $(patsubst %.c,$(BUILD)/%.o,$(SRCS))
ASMOBJS := $(patsubst %.asm,$(BUILD)/%.o,$(ASMSRCS))

# Vendored bits fetched by this Makefile.
LIMINE_DIR := limine
LIMINE_HOST := $(LIMINE_DIR)/limine
LIMINE_HEADER := kernel/limine.h

.PHONY: all run clean distclean
all: $(ISO)

# ----------------------------------------------------------------------------
# Vendoring Limine
# ----------------------------------------------------------------------------
# Download the pinned Limine binary release (BIOS + UEFI boot stages, the
# limine.c host deployer and its Makefile) and build the host `limine` tool.
$(LIMINE_HOST):
	rm -rf $(LIMINE_DIR)
	curl -fL https://github.com/limine-bootloader/limine/releases/download/$(LIMINE_VERSION)/limine-binary.tar.gz -o limine-binary.tar.gz
	mkdir -p $(LIMINE_DIR)
	tar -xzf limine-binary.tar.gz --strip-components=1 -C $(LIMINE_DIR)
	rm -f limine-binary.tar.gz
	$(MAKE) -C $(LIMINE_DIR)

# Download the matching Limine protocol header used by kernel.c. It is a single
# self-contained header (depends only on <stdint.h>).
$(LIMINE_HEADER):
	curl -fL https://raw.githubusercontent.com/limine-bootloader/limine-protocol/$(LIMINE_PROTO_COMMIT)/include/limine.h -o $(LIMINE_HEADER)

# ----------------------------------------------------------------------------
# Compile + link the kernel
# ----------------------------------------------------------------------------
# Each object depends on the downloaded header so the first build fetches it.
$(BUILD)/%.o: %.c $(LIMINE_HEADER)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.asm
	mkdir -p $(dir $@)
	$(AS) $(ASMFLAGS) $< -o $@

$(KERNEL_ELF): $(OBJS) $(ASMOBJS) linker.ld
	mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(OBJS) $(ASMOBJS) -o $@

# ----------------------------------------------------------------------------
# Stage the ISO tree and build a hybrid BIOS+UEFI bootable ISO
# ----------------------------------------------------------------------------
$(ISO): $(KERNEL_ELF) $(LIMINE_HOST) boot/limine.conf
	rm -rf $(BUILD)/iso_root
	# Kernel goes where limine.conf's `path:` points (boot():/boot/kernel).
	mkdir -p $(BUILD)/iso_root/boot/limine
	mkdir -p $(BUILD)/iso_root/EFI/BOOT
	cp $(KERNEL_ELF) $(BUILD)/iso_root/boot/kernel
	cp boot/limine.conf $(BUILD)/iso_root/boot/limine/limine.conf
	# BIOS boot stages.
	cp $(LIMINE_DIR)/limine-bios.sys      $(BUILD)/iso_root/boot/limine/
	cp $(LIMINE_DIR)/limine-bios-cd.bin   $(BUILD)/iso_root/boot/limine/
	# UEFI El Torito boot image + the EFI executable firmware looks for.
	cp $(LIMINE_DIR)/limine-uefi-cd.bin   $(BUILD)/iso_root/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI          $(BUILD)/iso_root/EFI/BOOT/
	cp $(LIMINE_DIR)/BOOTIA32.EFI         $(BUILD)/iso_root/EFI/BOOT/
	# Build the ISO: bootable via legacy BIOS (El Torito, no emulation) and via
	# UEFI (the --efi-boot image), with a protective MBR so it also works as a
	# raw USB image.
	xorriso -as mkisofs \
		-b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image \
		--protective-msdos-label \
		$(BUILD)/iso_root -o $(ISO)
	# Embed the BIOS boot stage into the ISO so legacy BIOS can boot it.
	$(LIMINE_HOST) bios-install $(ISO)

# ----------------------------------------------------------------------------
# Run in QEMU
# ----------------------------------------------------------------------------
# -serial stdio wires the guest's COM1 to this terminal, so serial_write()
# output appears right here. The graphical framebuffer opens in a QEMU window
# (via WSLg). -no-reboot/-no-shutdown keep a crash on screen instead of looping.
run: $(ISO)
	qemu-system-x86_64 \
		-M q35 \
		-m 512M \
		-cdrom $(ISO) \
		-boot d \
		-serial stdio \
		-no-reboot -no-shutdown

# ----------------------------------------------------------------------------
# Cleanup
# ----------------------------------------------------------------------------
clean:
	rm -rf $(BUILD) $(ISO)

distclean: clean
	rm -rf $(LIMINE_DIR) $(LIMINE_HEADER)
