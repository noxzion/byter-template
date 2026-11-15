#include "boot_catalog.h"

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;

#define CATALOG_MAGIC "BYTERCAT"
#define CATALOG_V1_HEADER_SIZE 24U
#define CATALOG_V1_ENTRY_SIZE 32U
#define CATALOG_V2_HEADER_SIZE 28U
#define CATALOG_V2_ENTRY_SIZE 40U
#define CATALOG_MODULE_SIZE 32U
#define MAX_LABEL_SIZE 63U
#define MAX_PATH_SIZE 1024U
#define MAX_STRING_SIZE 1024U
#define MAX_FILE_SIZE (64ULL * 1024 * 1024)

static u32 read_le32(const u8 *p) {
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static u64 read_le64(const u8 *p) {
    return (u64)read_le32(p) | (u64)read_le32(p + 4) << 32;
}

static int bytes_equal(const u8 *a, const char *b, u32 length) {
    for (u32 i = 0; i < length; ++i)
        if (a[i] != (u8)b[i]) return 0;
    return b[length] == 0;
}

static int valid_catalog_string(const u8 *catalog, u32 catalog_size,
                                u32 data_start, u32 offset, u32 length,
                                u32 maximum, int printable_ascii) {
    if (!length || length > maximum || offset < data_start || offset >= catalog_size ||
        length >= catalog_size - offset || catalog[offset + length] != 0) return 0;
    for (u32 i = 0; i < length; ++i) {
        if (!catalog[offset + i]) return 0;
        if (printable_ascii && (catalog[offset + i] < 0x20 || catalog[offset + i] > 0x7e))
            return 0;
    }
    return 1;
}

int byter_boot_catalog_parse(uint8_t *shim, uint64_t file_size,
                             uint8_t *catalog, uint64_t catalog_size,
                             struct byter_boot_option *options,
                             struct byter_boot_module *modules,
                             u32 *option_count) {
    if (!shim || !catalog || !options || !modules || !option_count ||
        (u64)shim > ~0ULL - file_size || (u64)catalog < (u64)shim ||
        (u64)catalog - (u64)shim > file_size ||
        catalog_size > file_size - ((u64)catalog - (u64)shim) ||
        catalog_size < CATALOG_V1_HEADER_SIZE) return 0;

    if (!bytes_equal(catalog, CATALOG_MAGIC, 8)) return 0;
    u32 version = read_le32(catalog + 8);
    u32 count = read_le32(catalog + 12);
    u32 entry_size = read_le32(catalog + 16);
    u32 total_size = read_le32(catalog + 20);
    u32 header_size, module_count = 0;
    if (version == 1) {
        header_size = CATALOG_V1_HEADER_SIZE;
        if (entry_size != CATALOG_V1_ENTRY_SIZE) return 0;
    } else if (version == 2) {
        header_size = CATALOG_V2_HEADER_SIZE;
        if (catalog_size < header_size || entry_size != CATALOG_V2_ENTRY_SIZE) return 0;
        module_count = read_le32(catalog + 24);
        if (module_count > BYTER_BOOT_MAX_MODULES) return 0;
    } else {
        return 0;
    }

    if (!count || count > BYTER_BOOT_MAX_OPTIONS || total_size != catalog_size ||
        count > (catalog_size - header_size) / entry_size ||
        module_count > (catalog_size - header_size - count * entry_size) / CATALOG_MODULE_SIZE)
        return 0;

    u32 module_table = header_size + count * entry_size;
    u32 data_start = module_table + module_count * CATALOG_MODULE_SIZE;
    u64 catalog_offset = (u64)catalog - (u64)shim;
    u64 catalog_end = catalog_offset + catalog_size;
    u64 elf_start[BYTER_BOOT_MAX_OPTIONS], elf_end[BYTER_BOOT_MAX_OPTIONS];
    u64 module_start[BYTER_BOOT_MAX_MODULES], module_end[BYTER_BOOT_MAX_MODULES];
    u8 module_seen[BYTER_BOOT_MAX_MODULES] = {0};

    for (u32 i = 0; i < count; ++i) {
        const u8 *entry = catalog + header_size + i * entry_size;
        u32 name_offset = read_le32(entry);
        u32 name_length = read_le32(entry + 4);
        u64 elf_offset = read_le64(entry + 8);
        u64 elf_size = read_le64(entry + 16);
        u32 cmdline_offset = read_le32(entry + 24);
        u32 cmdline_size = read_le32(entry + 28);
        u32 module_first = version == 2 ? read_le32(entry + 32) : 0;
        u32 option_modules = version == 2 ? read_le32(entry + 36) : 0;

        if (!valid_catalog_string(catalog, (u32)catalog_size, data_start,
                                  name_offset, name_length, MAX_LABEL_SIZE, 1) ||
            !cmdline_size || cmdline_size > MAX_STRING_SIZE + 1 ||
            cmdline_offset < data_start || cmdline_offset >= catalog_size ||
            cmdline_size > catalog_size - cmdline_offset ||
            catalog[cmdline_offset + cmdline_size - 1] != 0 ||
            !elf_size || elf_size > MAX_FILE_SIZE || elf_offset < catalog_end ||
            elf_offset > file_size || elf_size > file_size - elf_offset ||
            module_first > module_count || option_modules > module_count - module_first)
            return 0;

        for (u32 j = 0; j + 1 < cmdline_size; ++j)
            if (!catalog[cmdline_offset + j]) return 0;
        for (u32 j = 0; j < i; ++j) {
            if (elf_offset < elf_end[j] && elf_start[j] < elf_offset + elf_size)
                return 0;
            if (options[j].name && bytes_equal(catalog + name_offset,
                                                options[j].name, name_length)) return 0;
        }
        elf_start[i] = elf_offset;
        elf_end[i] = elf_offset + elf_size;
        options[i] = (struct byter_boot_option){
            .name = (const char *)(catalog + name_offset),
            .elf = shim + elf_offset,
            .elf_size = elf_size,
            .cmdline = catalog + cmdline_offset,
            .cmdline_size = cmdline_size,
            .modules = modules + module_first,
            .module_count = option_modules,
        };
        for (u32 j = 0; j < option_modules; ++j) {
            u32 index = module_first + j;
            if (module_seen[index]) return 0;
            module_seen[index] = 1;
        }
    }

    for (u32 i = 0; i < module_count; ++i) {
        const u8 *record = catalog + module_table + i * CATALOG_MODULE_SIZE;
        u32 path_offset = read_le32(record);
        u32 path_length = read_le32(record + 4);
        u32 string_offset = read_le32(record + 8);
        u32 string_length = read_le32(record + 12);
        u64 file_offset = read_le64(record + 16);
        u64 module_size = read_le64(record + 24);

        if (!module_seen[i] ||
            !valid_catalog_string(catalog, (u32)catalog_size, data_start,
                                  path_offset, path_length, MAX_PATH_SIZE, 0) ||
            !string_length || string_length > MAX_STRING_SIZE + 1 ||
            string_offset < data_start || string_offset >= catalog_size ||
            string_length > catalog_size - string_offset ||
            catalog[string_offset + string_length - 1] != 0 ||
            !module_size || module_size > MAX_FILE_SIZE || file_offset < catalog_end ||
            file_offset > file_size || module_size > file_size - file_offset)
            return 0;

        module_start[i] = file_offset;
        module_end[i] = file_offset + module_size;
        for (u32 j = 0; j + 1 < string_length; ++j)
            if (!catalog[string_offset + j]) return 0;
        for (u32 j = 0; j < i; ++j)
            if (file_offset < module_end[j] && module_start[j] < file_offset + module_size)
                return 0;
        for (u32 j = 0; j < count; ++j)
            if (file_offset < elf_end[j] && elf_start[j] < file_offset + module_size)
                return 0;
        modules[i] = (struct byter_boot_module){
            .path = (const char *)(catalog + path_offset),
            .string = (const char *)(catalog + string_offset),
            .data = shim + file_offset,
            .size = module_size,
        };
    }

    *option_count = count;
    return 1;
}
