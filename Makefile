CLANG ?= clang
LLD ?= ld.lld
FASTBOOT ?= fastboot
OUT ?= build
CONFIG ?= byter.cfg
ELF ?=
ASSETS_REPO ?= https://github.com/noxzion/byter-template.git
ASSETS_BRANCH ?= assets
ASSETS_DIR ?= .assets
ASSETS := $(ASSETS_DIR)/repo
ASSETS_STAMP := $(ASSETS_DIR)/.ready
PACKER := $(ASSETS)/tools/pack_android_boot.py
SHIM := $(ASSETS)/bin/boot-shim.bin
MKBOOTIMG := $(ASSETS)/tools/mkbootimg
CFLAGS := --target=aarch64-none-elf -O2 -ffreestanding -nostdlib -fpie -fno-builtin -fno-stack-protector -mgeneral-regs-only -mstrict-align -Wall -Wextra -Werror -I$(ASSETS)/Boot/include

.PHONY: all image kernel boot clean
all: image
image: $(OUT)/byter-boot.img
kernel: $(OUT)/kernel.elf

$(ASSETS_STAMP):
	mkdir -p $(ASSETS_DIR)
	git clone --depth=1 --single-branch --branch $(ASSETS_BRANCH) $(ASSETS_REPO) $(ASSETS)
	touch $@

$(OUT):
	mkdir -p $@

$(OUT)/main.o: src/main.c | $(ASSETS_STAMP) $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/kernel.elf: $(OUT)/main.o kernel.ld
	$(LLD) -T kernel.ld --build-id=none --no-undefined $< -o $@

$(OUT)/byter-boot.img: $(OUT)/kernel.elf $(CONFIG) | $(ASSETS_STAMP)
	python3 $(PACKER) --shim $(SHIM) --config $(CONFIG) $(if $(ELF),--elf $(ELF)) --kernel-out $(OUT)/byter-kernel.img --boot-out $@ --mkbootimg $(MKBOOTIMG)

boot: $(OUT)/byter-boot.img
	$(FASTBOOT) boot $<

clean:
	rm -rf $(OUT)
