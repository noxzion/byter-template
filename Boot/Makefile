CLANG ?= clang
LLD ?= ld.lld
OBJCOPY ?= llvm-objcopy
OUT ?= build
CFLAGS := --target=aarch64-none-elf -O2 -ffreestanding -nostdlib -fpie -fno-builtin -fno-stack-protector -mgeneral-regs-only -mstrict-align -Wall -Wextra -Werror -I. -Iinclude

ARCH := arch/aarch64
CORE := core

.PHONY: all shim clean
all: shim
shim: $(OUT)/boot-shim.bin

$(OUT):
	mkdir -p $(OUT)

$(OUT)/entry.o: $(ARCH)/entry.S | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/elf_loader.o: $(CORE)/elf_loader.c $(CORE)/console.h $(CORE)/boot_menu.h $(CORE)/boot_catalog.h include/byter.h $(ARCH)/page_tables.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/boot_catalog.o: $(CORE)/boot_catalog.c $(CORE)/boot_catalog.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/boot_menu.o: $(CORE)/boot_menu.c $(CORE)/boot_menu.h $(CORE)/boot_catalog.h $(CORE)/console.h platform/a059/buttons.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/a059_buttons.o: platform/a059/buttons.c platform/a059/buttons.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/console.o: $(CORE)/console.c $(CORE)/console.h $(CORE)/font8x16_basic.h include/byter.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/a059_framebuffer.o: platform/a059/framebuffer.c platform/a059/framebuffer.h include/byter.h | $(OUT)
	$(CLANG) $(CFLAGS) -Iplatform/a059 -c $< -o $@

$(OUT)/page_tables.o: $(ARCH)/page_tables.c $(ARCH)/page_tables.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/boot-shim.elf: $(OUT)/entry.o $(OUT)/elf_loader.o $(OUT)/console.o $(OUT)/boot_catalog.o $(OUT)/boot_menu.o $(OUT)/a059_buttons.o $(OUT)/a059_framebuffer.o $(OUT)/page_tables.o $(ARCH)/image.ld
	$(LLD) -T $(ARCH)/image.ld --build-id=none --no-undefined $(OUT)/entry.o $(OUT)/elf_loader.o $(OUT)/console.o $(OUT)/boot_catalog.o $(OUT)/boot_menu.o $(OUT)/a059_buttons.o $(OUT)/a059_framebuffer.o $(OUT)/page_tables.o -o $@

$(OUT)/boot-shim.bin: $(OUT)/boot-shim.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(OUT)
