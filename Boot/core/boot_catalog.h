#ifndef BYTER_BOOT_CATALOG_H
#define BYTER_BOOT_CATALOG_H

#include <stdint.h>

#define BYTER_BOOT_MAX_OPTIONS 16U
#define BYTER_BOOT_MAX_MODULES 128U

struct byter_boot_module {
    const char *path;
    const char *string;
    uint8_t *data;
    uint64_t size;
};

struct byter_boot_option {
    const char *name;
    uint8_t *elf;
    uint64_t elf_size;
    uint8_t *cmdline;
    uint64_t cmdline_size;
    struct byter_boot_module *modules;
    uint32_t module_count;
};

int byter_boot_catalog_parse(uint8_t *shim, uint64_t file_size,
                             uint8_t *catalog, uint64_t catalog_size,
                             struct byter_boot_option *options,
                             struct byter_boot_module *modules,
                             uint32_t *option_count);

#endif
