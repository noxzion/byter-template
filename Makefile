CLANG ?= clang
LLD ?= ld.lld
OBJCOPY ?= llvm-objcopy
MKBOOTIMG ?= mkbootimg
FASTBOOT ?= fastboot
OUT ?= build
CONFIG ?= byter.cfg
ELF ?=
RAW_BASE ?= https://raw.githubusercontent.com/noxzion/byter/master

CFLAGS := --target=aarch64-none-elf -O2 -ffreestanding -nostdlib -fpie -fno-builtin -fno-stack-protector -mgeneral-regs-only -mstrict-align -Wall -Wextra -Werror -Ibyter -Iloader/Boot/include -Iloader/Boot/arch/aarch64 -Iloader/Boot/platform/a059
PACKER := tools/pack_android_boot.py
LOADER := loader/Boot
SHIM_OBJECTS := $(OUT)/shim-entry.o $(OUT)/shim-elf-loader.o $(OUT)/shim-console.o $(OUT)/shim-framebuffer.o $(OUT)/shim-page-tables.o

.PHONY: all image kernel boot clean
all: image
image: $(OUT)/byter-boot.img
kernel: $(OUT)/kernel.elf

byter/byter.h:
	mkdir -p $(dir $@)
	curl -L --fail --silent --show-error "$(RAW_BASE)/Boot/include/byter.h" -o $@

$(PACKER):
	mkdir -p $(dir $@)
	curl -L --fail --silent --show-error "$(RAW_BASE)/tools/pack_android_boot.py" -o $@

$(OUT):
	mkdir -p $@

loader/%:
	mkdir -p $(dir $@)
	curl -L --fail --silent --show-error "$(RAW_BASE)/$*" -o $@

$(OUT)/main.o: src/main.c byter/byter.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/kernel.elf: $(OUT)/main.o kernel.ld
	$(LLD) -T kernel.ld --build-id=none --no-undefined $< -o $@

$(OUT)/shim-entry.o: $(LOADER)/arch/aarch64/entry.S | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/shim-elf-loader.o: $(LOADER)/core/elf_loader.c $(LOADER)/include/byter.h $(LOADER)/core/console.h $(LOADER)/arch/aarch64/page_tables.h $(LOADER)/platform/a059/framebuffer.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/shim-console.o: $(LOADER)/core/console.c $(LOADER)/core/console.h $(LOADER)/core/font8x16_basic.h $(LOADER)/include/byter.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/shim-framebuffer.o: $(LOADER)/platform/a059/framebuffer.c $(LOADER)/platform/a059/framebuffer.h $(LOADER)/include/byter.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/shim-page-tables.o: $(LOADER)/arch/aarch64/page_tables.c $(LOADER)/arch/aarch64/page_tables.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/boot-shim.elf: $(SHIM_OBJECTS) $(LOADER)/arch/aarch64/image.ld
	$(LLD) -T $(LOADER)/arch/aarch64/image.ld --build-id=none --no-undefined $(SHIM_OBJECTS) -o $@

$(OUT)/boot-shim.bin: $(OUT)/boot-shim.elf
	$(OBJCOPY) -O binary $< $@

$(OUT)/byter-boot.img: $(OUT)/boot-shim.bin $(OUT)/kernel.elf $(CONFIG) $(PACKER) $(if $(ELF),$(ELF))
	python3 $(PACKER) --shim $(OUT)/boot-shim.bin --config $(CONFIG) $(if $(ELF),--elf $(ELF)) --kernel-out $(OUT)/byter-kernel.img --boot-out $@ --mkbootimg $(MKBOOTIMG)

boot: $(OUT)/byter-boot.img
	$(FASTBOOT) boot $<

clean:
	rm -rf $(OUT)
