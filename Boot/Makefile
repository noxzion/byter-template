CLANG ?= clang
LLD ?= ld.lld
OBJCOPY ?= llvm-objcopy
MKBOOTIMG ?= mkbootimg
OUT ?= build
CONFIG ?= byter.cfg
CFLAGS := --target=aarch64-none-elf -O2 -ffreestanding -nostdlib -fpie -fno-builtin -fno-stack-protector -mgeneral-regs-only -mstrict-align -Wall -Wextra -Werror -I. -Iinclude

ARCH := arch/aarch64
CORE := core
PACKER := ../tools/pack_android_boot.py

.PHONY: all image shim kernel boot diagnostic clean FORCE
all: image
image: $(OUT)/byter-boot.img
shim: $(OUT)/boot-shim.bin
kernel: $(OUT)/kernel.elf

$(OUT):
	mkdir -p $(OUT)

$(OUT)/entry.o: $(ARCH)/entry.S | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/elf_loader.o: $(CORE)/elf_loader.c $(CORE)/console.h include/byter.h $(ARCH)/page_tables.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/console.o: $(CORE)/console.c $(CORE)/console.h $(CORE)/font8x16_basic.h include/byter.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/a059_framebuffer.o: platform/a059/framebuffer.c platform/a059/framebuffer.h include/byter.h | $(OUT)
	$(CLANG) $(CFLAGS) -Iplatform/a059 -c $< -o $@

$(OUT)/page_tables.o: $(ARCH)/page_tables.c $(ARCH)/page_tables.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/boot-shim.elf: $(OUT)/entry.o $(OUT)/elf_loader.o $(OUT)/console.o $(OUT)/a059_framebuffer.o $(OUT)/page_tables.o $(ARCH)/image.ld
	$(LLD) -T $(ARCH)/image.ld --build-id=none --no-undefined $(OUT)/entry.o $(OUT)/elf_loader.o $(OUT)/console.o $(OUT)/a059_framebuffer.o $(OUT)/page_tables.o -o $@

$(OUT)/boot-shim.bin: $(OUT)/boot-shim.elf
	$(OBJCOPY) -O binary $< $@

$(OUT)/kernel.o: ../example/main.c ../example/fdt_diag.h include/byter.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/fdt_diag.o: ../example/fdt_diag.c ../example/fdt_diag.h include/byter.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/kernel_vectors.o: ../example/vectors.S | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/kernel.elf: $(OUT)/kernel.o $(OUT)/fdt_diag.o $(OUT)/kernel_vectors.o ../example/kernel.ld
	$(LLD) -T ../example/kernel.ld --build-id=none --no-undefined $(OUT)/kernel.o $(OUT)/fdt_diag.o $(OUT)/kernel_vectors.o -o $@

FORCE:

$(OUT)/byter-boot.img: FORCE $(OUT)/boot-shim.bin $(OUT)/kernel.elf $(if $(ELF),$(ELF)) $(CONFIG) $(PACKER)
	python3 $(PACKER) --shim $(OUT)/boot-shim.bin --config $(CONFIG) $(if $(ELF),--elf $(ELF)) --kernel-out $(OUT)/byter-kernel.img --boot-out $@ --mkbootimg $(MKBOOTIMG)

boot: image
	fastboot boot $(OUT)/byter-boot.img

$(OUT)/byter-diagnostic.img: FORCE $(OUT)/boot-shim.bin $(OUT)/kernel.elf $(if $(ELF),$(ELF)) $(CONFIG) $(PACKER)
	python3 $(PACKER) --shim $(OUT)/boot-shim.bin --config $(CONFIG) $(if $(ELF),--elf $(ELF)) --kernel-out $(OUT)/byter-diagnostic-kernel.img --boot-out $@ --mkbootimg $(MKBOOTIMG)

diagnostic: $(OUT)/byter-diagnostic.img
	fastboot boot $(OUT)/byter-diagnostic.img

clean:
	rm -rf $(OUT)
