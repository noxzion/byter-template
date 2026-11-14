#include "byter.h"
#include "../platform/a059/framebuffer.h"
#include "../arch/aarch64/page_tables.h"
#include "console.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

void *memcpy(void *destination, const void *source, u64 length) {
    volatile u8 *out = destination;
    const u8 *in = source;
    for (u64 i = 0; i < length; ++i) out[i] = in[i];
    return destination;
}

#define MAX_RANGES 512
#define MAX_OUT_RANGES 512
#define MAX_PHDRS 128
#define MAX_MAP_BOUNDARIES (2 + 2 * (MAX_RANGES + MAX_PHDRS + MAX_OUT_RANGES + 6))
#define MAX_IMAGE_SIZE (64ULL * 1024 * 1024)
#define MAX_ELF_MEMORY (256ULL * 1024 * 1024)
#define HANDOFF_OFFSET 0x100000ULL
#define HANDOFF_SIZE 0x10000ULL
#define STACK_SIZE 0x30000ULL
#define MAX_CMDLINE_SIZE 1024ULL

typedef struct { u64 base, length; } range;
typedef struct { u64 entry, ttbr0, ttbr1, tcr; } boot_context;
typedef struct {
    u64 address, size, width, height, pitch;
    u8 red_size, red_shift, green_size, green_shift, blue_size, blue_shift;
    u8 format_valid;
} framebuffer_info;

typedef struct {
    u32 address_cells, size_cells;
    const u8 *ranges;
    u32 ranges_size;
    u8 has_ranges, excluded;
} fdt_node_context;

static framebuffer_info status_framebuffer;

static int console_ready;

static void sync_status_console(void) {
    if (!status_framebuffer.format_valid || !status_framebuffer.address) return;
    struct byter_framebuffer fb = {
        .address = (void *)(u64)status_framebuffer.address,
        .width = status_framebuffer.width,
        .height = status_framebuffer.height,
        .pitch = status_framebuffer.pitch,
        .bpp = 32,
        .memory_model = BYTER_FRAMEBUFFER_RGB,
        .red_mask_size = status_framebuffer.red_size,
        .red_mask_shift = status_framebuffer.red_shift,
        .green_mask_size = status_framebuffer.green_size,
        .green_mask_shift = status_framebuffer.green_shift,
        .blue_mask_size = status_framebuffer.blue_size,
        .blue_mask_shift = status_framebuffer.blue_shift,
    };
    byter_console_set_framebuffer(&fb);
    console_ready = 1;
}

static void set_status_framebuffer(const struct byter_framebuffer *fb) {
    if (!fb) return;
    status_framebuffer = (framebuffer_info){
        .address = (u64)fb->address,
        .size = fb->pitch * fb->height,
        .width = fb->width,
        .height = fb->height,
        .pitch = fb->pitch,
        .red_size = fb->red_mask_size,
        .red_shift = fb->red_mask_shift,
        .green_size = fb->green_mask_size,
        .green_shift = fb->green_mask_shift,
        .blue_size = fb->blue_mask_size,
        .blue_shift = fb->blue_mask_shift,
        .format_valid = fb->memory_model == BYTER_FRAMEBUFFER_RGB && fb->bpp == 32,
    };
    sync_status_console();
}

static void log_boot_stage(const char *message) {
    if (!console_ready || !message) return;
    byter_console_write(message);
    byter_console_write("\n");
}

void byter_shim_handoff_stage(void) {
    log_boot_stage("Entering kernel");
}

void byter_shim_el2_drop_stage(void) {
    log_boot_stage("Dropping from EL2 to EL1");
}

typedef struct { u32 type, flags; u64 offset, vaddr, paddr, filesz, memsz, align; } elf_program;
typedef struct {
    u8 ident[16]; u16 type, machine; u32 version; u64 entry, phoff, shoff;
    u32 flags; u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} elf_header;
void *memset(void *dst, int value, unsigned long size) {
    volatile u8 *p = (volatile u8 *)dst;
    for (unsigned long i = 0; i < size; ++i) p[i] = (u8)value;
    return dst;
}

static u32 be32(const u8 *p) {
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static u64 cells_value(const u8 *p, u32 cells) {
    u64 value = 0;
    for (u32 i = 0; i < cells; ++i) value = (value << 32) | be32(p + i * 4);
    return value;
}

static int equal(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static int valid_string(const char *table, u32 size, u32 offset) {
    if (offset >= size) return 0;
    for (u32 i = offset; i < size; ++i) if (!table[i]) return 1;
    return 0;
}

static int begins_memory(const u8 *name) {
    return name[0] == 'm' && name[1] == 'e' && name[2] == 'm' && name[3] == 'o' &&
        name[4] == 'r' && name[5] == 'y' && (name[6] == 0 || name[6] == '@');
}

static int begins_framebuffer(const u8 *name) {
    static const char prefix[] = "framebuffer";
    for (u32 i = 0; i < sizeof(prefix) - 1; ++i)
        if (name[i] != (u8)prefix[i]) return 0;
    return name[sizeof(prefix) - 1] == 0 || name[sizeof(prefix) - 1] == '@';
}

static int string_list_has(const u8 *strings, u32 length, const char *wanted) {
    for (u32 offset = 0; offset < length;) {
        u32 end = offset;
        while (end < length && strings[end]) ++end;
        if (end == length) return 0;
        u32 i = 0;
        while (offset + i < end && wanted[i] && strings[offset + i] == (u8)wanted[i]) ++i;
        if (offset + i == end && wanted[i] == 0) return 1;
        offset = end + 1;
    }
    return 0;
}

static u32 property_u32(const u8 *data, u32 length) {
    return length == 4 ? be32(data) : 0;
}

static int parse_framebuffer_format(framebuffer_info *fb, const u8 *data, u32 length) {
    if (length != 9 || data[length - 1] != 0) return 0;
    if (data[0] == 'a' && data[1] == '8' && data[2] == 'r' && data[3] == '8' &&
        data[4] == 'g' && data[5] == '8' && data[6] == 'b' && data[7] == '8' && data[8] == 0) {
        fb->red_size = 8; fb->red_shift = 16;
        fb->green_size = 8; fb->green_shift = 8;
        fb->blue_size = 8; fb->blue_shift = 0;
        fb->format_valid = 1;
        return 1;
    }
    if (data[0] == 'a' && data[1] == '8' && data[2] == 'b' && data[3] == '8' &&
        data[4] == 'g' && data[5] == '8' && data[6] == 'r' && data[7] == '8' && data[8] == 0) {
        fb->red_size = 8; fb->red_shift = 0;
        fb->green_size = 8; fb->green_shift = 8;
        fb->blue_size = 8; fb->blue_shift = 16;
        fb->format_valid = 1;
        return 1;
    }
    return 0;
}

static int add_regs(range *out, u32 *count, const u8 *data, u32 length, u32 ac, u32 sc) {
    if (ac == 0 || ac > 2 || sc == 0 || sc > 2 || length % ((ac + sc) * 4)) return 0;
    for (u32 off = 0; off < length; off += (ac + sc) * 4) {
        u64 base = cells_value(data + off, ac);
        u64 size = cells_value(data + off + ac * 4, sc);
        if (!size) continue;
        if (base > ~0ULL - size || *count == MAX_RANGES) return 0;
        out[(*count)++] = (range){base, size};
    }
    return 1;
}

/* Parse FDT RAM, reservations, and framebuffer metadata. */
static int parse_fdt(u8 *dtb, u32 *total_size, range *memory, u32 *memory_count,
                     range *reserved, u32 *reserved_count, framebuffer_info *framebuffer,
                     u32 *root_address_cells, u32 *root_size_cells) {
    if (!dtb || be32(dtb) != 0xd00dfeed) return 0;
    u32 total = be32(dtb + 4), struct_off = be32(dtb + 8), strings_off = be32(dtb + 12);
    u32 reserve_off = be32(dtb + 16);
    u32 strings_size = be32(dtb + 32), struct_size = be32(dtb + 36);
    if (total < 40 || total > 16 * 1024 * 1024 || struct_off > total || struct_size > total - struct_off ||
        strings_off > total || strings_size > total - strings_off || reserve_off < 40 ||
        (reserve_off & 7) || reserve_off > total - 16) return 0;

    *memory_count = 0;
    *reserved_count = 0;
    u32 reserve_cursor = reserve_off;
    for (;;) {
        if (reserve_cursor > total - 16) return 0;
        u64 base = cells_value(dtb + reserve_cursor, 2);
        u64 size = cells_value(dtb + reserve_cursor + 8, 2);
        reserve_cursor += 16;
        if (!base && !size) break;
        if (!size || base > ~0ULL - size || *reserved_count == MAX_RANGES) return 0;
        reserved[(*reserved_count)++] = (range){base, size};
    }

    u8 *st = dtb + struct_off, *st_end = st + struct_size;
    const char *strings = (const char *)(dtb + strings_off);
    u32 ac = 2, sc = 2, depth = 0;
    int ended = 0;

    /* Root cell widths are needed before parsing any child reg properties. */
    for (u8 *p = st; p + 4 <= st_end;) {
        u32 token = be32(p); p += 4;
        if (token == 1) {
            while (p < st_end && *p) ++p;
            if (p >= st_end) return 0;
            p = (u8 *)(((u64)(p + 1) + 3) & ~3ULL);
            ++depth;
        } else if (token == 2) {
            if (!depth) return 0;
            --depth;
        } else if (token == 3) {
            if (p + 8 > st_end) return 0;
            u32 length = be32(p), nameoff = be32(p + 4); p += 8;
            if (length > (u32)(st_end - p) || !valid_string(strings, strings_size, nameoff)) return 0;
            const char *name = strings + nameoff;
            if (depth == 1) {
                if (equal(name, "#address-cells")) { if (length != 4) return 0; ac = be32(p); }
                if (equal(name, "#size-cells")) { if (length != 4) return 0; sc = be32(p); }
            }
            p = (u8 *)(((u64)(p + length) + 3) & ~3ULL);
        } else if (token == 4) {
        } else if (token == 9) { ended = 1; break; }
        else return 0;
    }
    if (!ended || depth || ac == 0 || ac > 2 || sc == 0 || sc > 2) return 0;
    *root_address_cells = ac;
    *root_size_cells = sc;

    u8 memory_node[32] = {0}, reserved_parent[32] = {0}, reserved_node[32] = {0};
    u8 chosen_node[32] = {0};
    u8 framebuffer_node[32] = {0}, framebuffer_compatible[32] = {0};
    framebuffer_info node_framebuffer[32] = {{0}};
    u32 node_ac[32] = {0}, node_sc[32] = {0};
    node_ac[0] = ac; node_sc[0] = sc;
    depth = 0; ended = 0; *memory_count = 0;
    *framebuffer = (framebuffer_info){0};
    for (u8 *p = st; p + 4 <= st_end;) {
        u32 token = be32(p); p += 4;
        if (token == 1) {
            if (depth + 1 >= 32) return 0;
            u8 *name = p;
            while (p < st_end && *p) ++p;
            if (p >= st_end) return 0;
            p = (u8 *)(((u64)(p + 1) + 3) & ~3ULL);
            ++depth;
            node_ac[depth] = depth == 1 ? ac : node_ac[depth - 1];
            node_sc[depth] = depth == 1 ? sc : node_sc[depth - 1];
            memory_node[depth] = depth == 2 && begins_memory(name);
            reserved_parent[depth] = depth == 2 && equal((const char *)name, "reserved-memory");
            reserved_node[depth] = depth == 3 && reserved_parent[depth - 1];
            chosen_node[depth] = depth == 2 && equal((const char *)name, "chosen");
            framebuffer_node[depth] = depth == 3 && chosen_node[depth - 1] && begins_framebuffer(name);
        } else if (token == 2) {
            if (!depth) return 0;
            if (framebuffer_node[depth] && framebuffer_compatible[depth] &&
                !framebuffer->format_valid) {
                framebuffer_info *candidate = &node_framebuffer[depth];
                if (candidate->address && candidate->size && candidate->width && candidate->height &&
                    candidate->pitch && candidate->format_valid &&
                    candidate->width <= 16384 && candidate->height <= 16384 &&
                    candidate->pitch >= candidate->width * 4 &&
                    candidate->pitch % 4 == 0 &&
                    candidate->height <= ~0ULL / candidate->pitch &&
                    candidate->size >= candidate->pitch * candidate->height &&
                    candidate->address <= ~0ULL - candidate->size)
                    *framebuffer = *candidate;
            }
            --depth;
        } else if (token == 3) {
            if (p + 8 > st_end) return 0;
            u32 length = be32(p), nameoff = be32(p + 4); p += 8;
            if (length > (u32)(st_end - p) || !valid_string(strings, strings_size, nameoff)) return 0;
            const char *name = strings + nameoff;
            if (depth && equal(name, "#address-cells")) {
                if (length != 4) return 0;
                node_ac[depth] = be32(p);
            }
            if (depth && equal(name, "#size-cells")) {
                if (length != 4) return 0;
                node_sc[depth] = be32(p);
            }
            if (depth && framebuffer_node[depth]) {
                framebuffer_info *candidate = &node_framebuffer[depth];
                if (equal(name, "compatible"))
                    framebuffer_compatible[depth] = string_list_has(p, length, "simple-framebuffer");
                else if (equal(name, "reg")) {
                    u32 ac = node_ac[depth - 1], sc = node_sc[depth - 1];
                    if (ac && ac <= 2 && sc && sc <= 2 && length == (ac + sc) * 4) {
                        candidate->address = cells_value(p, ac);
                        candidate->size = cells_value(p + ac * 4, sc);
                    }
                } else if (equal(name, "width")) candidate->width = property_u32(p, length);
                else if (equal(name, "height")) candidate->height = property_u32(p, length);
                else if (equal(name, "stride")) candidate->pitch = property_u32(p, length);
                else if (equal(name, "format")) parse_framebuffer_format(candidate, p, length);
            }
            if (depth && equal(name, "reg")) {
                if (memory_node[depth] && !add_regs(memory, memory_count, p, length, node_ac[depth - 1], node_sc[depth - 1])) return 0;
                if (reserved_node[depth]) {
                    if (!add_regs(reserved, reserved_count, p, length, node_ac[depth - 1], node_sc[depth - 1])) return 0;
                }
            }
            p = (u8 *)(((u64)(p + length) + 3) & ~3ULL);
            if (p > st_end) return 0;
        } else if (token == 4) {
        } else if (token == 9) { ended = 1; break; }
        else return 0;
    }
    *total_size = total;
    return ended && depth == 0 && *memory_count != 0;
}

static int address_fits_cells(u64 address, u64 length, u32 cells) {
    if (!length || address > ~0ULL - (length - 1)) return 0;
    if (cells == 1) return address <= 0xffffffffULL && length - 1 <= 0xffffffffULL - address;
    return cells == 2;
}

static int translate_fdt_range(fdt_node_context *nodes, u32 node_depth,
                               range input, range *output) {
    if (!input.length || input.base > ~0ULL - (input.length - 1)) return 0;
    u64 address = input.base;
    for (u32 bus_depth = node_depth - 1; bus_depth > 1; --bus_depth) {
        fdt_node_context *bus = &nodes[bus_depth];
        u32 parent_cells = nodes[bus_depth - 1].address_cells;
        if (!bus->has_ranges) return 0;
        if (!bus->ranges_size) {
            if (!address_fits_cells(address, input.length, parent_cells)) return 0;
            continue;
        }

        u32 child_cells = bus->address_cells;
        u32 size_cells = bus->size_cells;
        if (!child_cells || child_cells > 2 || !parent_cells || parent_cells > 2 ||
            !size_cells || size_cells > 2) return 0;
        u32 tuple_cells = child_cells + parent_cells + size_cells;
        u32 tuple_size = tuple_cells * 4;
        if (!tuple_size || bus->ranges_size % tuple_size) return 0;

        int mapped = 0;
        for (u32 offset = 0; offset < bus->ranges_size; offset += tuple_size) {
            const u8 *entry = bus->ranges + offset;
            u64 child_base = cells_value(entry, child_cells);
            u64 parent_base = cells_value(entry + child_cells * 4, parent_cells);
            u64 span = cells_value(entry + (child_cells + parent_cells) * 4, size_cells);
            if (!span || child_base > ~0ULL - span || address < child_base) continue;
            u64 delta = address - child_base;
            if (delta > span || input.length > span - delta || parent_base > ~0ULL - delta)
                continue;
            address = parent_base + delta;
            if (!address_fits_cells(address, input.length, parent_cells)) continue;
            mapped = 1;
            break;
        }
        if (!mapped) return 0;
    }
    *output = (range){address, input.length};
    return 1;
}

static int parse_fdt_device_ranges(u8 *dtb, u32 total_size, u32 root_address_cells,
                                   u32 root_size_cells, range *ranges, u32 *range_count) {
    u32 struct_off = be32(dtb + 8), strings_off = be32(dtb + 12);
    u32 strings_size = be32(dtb + 32), struct_size = be32(dtb + 36);
    if (struct_off > total_size || struct_size > total_size - struct_off ||
        strings_off > total_size || strings_size > total_size - strings_off) return 0;
    const u8 *st = dtb + struct_off, *st_end = st + struct_size;
    const char *strings = (const char *)(dtb + strings_off);
    fdt_node_context nodes[32] = {{0}};
    u32 depth = 0;
    int ended = 0;
    for (const u8 *p = st; p + 4 <= st_end;) {
        u32 token = be32(p); p += 4;
        if (token == 1) {
            if (depth + 1 >= 32) return 0;
            const u8 *name = p;
            while (p < st_end && *p) ++p;
            if (p >= st_end) return 0;
            p = (const u8 *)(((u64)(p + 1) + 3) & ~3ULL);
            ++depth;
            nodes[depth] = (fdt_node_context){
                .address_cells = depth == 1 ? root_address_cells : nodes[depth - 1].address_cells,
                .size_cells = depth == 1 ? root_size_cells : nodes[depth - 1].size_cells,
                .excluded = depth > 1 && nodes[depth - 1].excluded,
            };
            if (depth == 2 && (begins_memory(name) ||
                equal((const char *)name, "reserved-memory") ||
                equal((const char *)name, "chosen") || equal((const char *)name, "cpus")))
                nodes[depth].excluded = 1;
        } else if (token == 2) {
            if (!depth) return 0;
            --depth;
        } else if (token == 3) {
            if (p + 8 > st_end) return 0;
            u32 length = be32(p), nameoff = be32(p + 4); p += 8;
            if (length > (u32)(st_end - p) || !valid_string(strings, strings_size, nameoff)) return 0;
            const char *name = strings + nameoff;
            if (depth && equal(name, "#address-cells"))
                nodes[depth].address_cells = length == 4 ? be32(p) : 0;
            else if (depth && equal(name, "#size-cells"))
                nodes[depth].size_cells = length == 4 ? be32(p) : 0;
            else if (depth && equal(name, "ranges")) {
                nodes[depth].has_ranges = 1;
                nodes[depth].ranges = p;
                nodes[depth].ranges_size = length;
            } else if (depth > 1 && !nodes[depth].excluded && equal(name, "reg")) {
                u32 address_cells = nodes[depth - 1].address_cells;
                u32 size_cells = nodes[depth - 1].size_cells;
                if (address_cells && address_cells <= 2 && size_cells && size_cells <= 2) {
                    u32 tuple_size = (address_cells + size_cells) * 4;
                    if (length % tuple_size == 0) {
                        for (u32 offset = 0; offset < length; offset += tuple_size) {
                            const u8 *entry = p + offset;
                            range physical = {
                                cells_value(entry, address_cells),
                                cells_value(entry + address_cells * 4, size_cells)
                            };
                            range translated;
                            if (!physical.length || !translate_fdt_range(nodes, depth, physical, &translated))
                                continue;
                            /* Device metadata is optional; keep booting if the bounded list fills. */
                            if (*range_count < MAX_RANGES)
                                ranges[(*range_count)++] = translated;
                        }
                    }
                }
            }
            p = (const u8 *)(((u64)(p + length) + 3) & ~3ULL);
            if (p > st_end) return 0;
        } else if (token == 4) {
        } else if (token == 9) {
            ended = 1;
            break;
        } else {
            return 0;
        }
    }
    return ended && depth == 0;
}

static int subtract_range(range *ranges, u32 *count, range cut) {
    if (!cut.length || cut.base > ~0ULL - cut.length) return 0;
    u64 cut_end = cut.base + cut.length;
    for (u32 i = 0; i < *count;) {
        range r = ranges[i];
        u64 end = r.base + r.length;
        if (cut.base >= end || cut_end <= r.base) { ++i; continue; }
        int left = cut.base > r.base, right = cut_end < end;
        if (left && right) {
            if (*count == MAX_OUT_RANGES) return 0;
            for (u32 j = *count; j > i + 1; --j) ranges[j] = ranges[j - 1];
            ranges[i] = (range){r.base, cut.base - r.base};
            ranges[i + 1] = (range){cut_end, end - cut_end};
            ++*count; i += 2;
        } else if (left) {
            ranges[i].length = cut.base - r.base; ++i;
        } else if (right) {
            ranges[i] = (range){cut_end, end - cut_end}; ++i;
        } else {
            for (u32 j = i + 1; j < *count; ++j) ranges[j - 1] = ranges[j];
            --*count;
        }
    }
    return 1;
}

static int in_memory(range *memory, u32 count, range candidate) {
    if (!candidate.length || candidate.base > ~0ULL - candidate.length) return 0;
    u64 end = candidate.base + candidate.length;
    for (u32 i = 0; i < count; ++i)
        if (candidate.base >= memory[i].base && end <= memory[i].base + memory[i].length) return 1;
    return 0;
}

static int overlaps(range a, range b) {
    return a.base < b.base + b.length && b.base < a.base + a.length;
}

static int page_aligned_range(range input, range *output) {
    if (!input.length || input.base > ~0ULL - input.length) return 0;
    u64 end = input.base + input.length;
    if (end > ~0ULL - 0xfff) return 0;
    output->base = input.base & ~0xfffULL;
    u64 aligned_end = (end + 0xfff) & ~0xfffULL;
    output->length = aligned_end - output->base;
    return output->length != 0;
}

static int page_aligned_inward_range(range input, range *output) {
    if (!input.length || input.base > ~0ULL - input.length) return 0;
    u64 end = input.base + input.length;
    if (input.base > ~0ULL - 0xfff) return 0;
    u64 start = (input.base + 0xfff) & ~0xfffULL;
    end &= ~0xfffULL;
    if (end <= start) return 0;
    *output = (range){start, end - start};
    return 1;
}

static int normalize_memory(range *memory, u32 *count) {
    for (u32 i = 1; i < *count; ++i) {
        range current = memory[i];
        u32 j = i;
        while (j && memory[j - 1].base > current.base) {
            memory[j] = memory[j - 1];
            --j;
        }
        memory[j] = current;
    }
    u32 normalized = 0;
    for (u32 i = 0; i < *count; ++i) {
        range current = memory[i];
        if (!current.length || current.base > ~0ULL - current.length) return 0;
        if (normalized) {
            range *previous = &memory[normalized - 1];
            u64 previous_end = previous->base + previous->length;
            if (current.base < previous_end) return 0;
            if (current.base == previous_end) {
                previous->length += current.length;
                continue;
            }
        }
        memory[normalized++] = current;
    }
    *count = normalized;
    return normalized != 0;
}

static int normalize_ranges(range *ranges, u32 *count) {
    for (u32 i = 1; i < *count; ++i) {
        range current = ranges[i];
        u32 j = i;
        while (j && ranges[j - 1].base > current.base) {
            ranges[j] = ranges[j - 1];
            --j;
        }
        ranges[j] = current;
    }
    u32 normalized = 0;
    for (u32 i = 0; i < *count; ++i) {
        range current = ranges[i];
        if (!current.length || current.base > ~0ULL - current.length) return 0;
        if (normalized) {
            range *previous = &ranges[normalized - 1];
            u64 previous_end = previous->base + previous->length;
            u64 current_end = current.base + current.length;
            if (current.base <= previous_end) {
                if (current_end > previous_end) previous->length = current_end - previous->base;
                continue;
            }
        }
        ranges[normalized++] = current;
    }
    *count = normalized;
    return 1;
}

static int contains_range(range region, range candidate) {
    return candidate.base >= region.base && candidate.base <= ~0ULL - candidate.length &&
           region.base <= ~0ULL - region.length &&
           candidate.base + candidate.length <= region.base + region.length;
}

static int add_map_boundaries(u64 *boundaries, u32 *count, range memory, range cut) {
    if (!cut.length || cut.base > ~0ULL - cut.length || !overlaps(memory, cut)) return 1;
    u64 start = cut.base > memory.base ? cut.base : memory.base;
    u64 memory_end = memory.base + memory.length;
    u64 cut_end = cut.base + cut.length;
    u64 end = cut_end < memory_end ? cut_end : memory_end;
    if (start > memory.base) boundaries[(*count)++] = start;
    if (end < memory_end) boundaries[(*count)++] = end;
    return *count <= MAX_MAP_BOUNDARIES;
}

static void sort_boundaries(u64 *boundaries, u32 count) {
    for (u32 i = 1; i < count; ++i) {
        u64 current = boundaries[i];
        u32 j = i;
        while (j && boundaries[j - 1] > current) {
            boundaries[j] = boundaries[j - 1];
            --j;
        }
        boundaries[j] = current;
    }
}

static int append_map_entry(struct byter_memmap_entry *entries, u32 *count,
                            range region, u64 type) {
    if (!region.length) return 1;
    if (*count && entries[*count - 1].type == type &&
        entries[*count - 1].base + entries[*count - 1].length == region.base) {
        entries[*count - 1].length += region.length;
        return 1;
    }
    if (*count == MAX_OUT_RANGES) return 0;
    entries[(*count)++] = (struct byter_memmap_entry){region.base, region.length, type};
    return 1;
}

static int sort_and_merge_map_entries(struct byter_memmap_entry *entries, u32 *count) {
    for (u32 i = 1; i < *count; ++i) {
        struct byter_memmap_entry current = entries[i];
        u32 j = i;
        while (j && entries[j - 1].base > current.base) {
            entries[j] = entries[j - 1];
            --j;
        }
        entries[j] = current;
    }
    u32 normalized = 0;
    for (u32 i = 0; i < *count; ++i) {
        struct byter_memmap_entry current = entries[i];
        if (!current.length || current.base > ~0ULL - current.length) return 0;
        if (normalized) {
            struct byter_memmap_entry *previous = &entries[normalized - 1];
            u64 previous_end = previous->base + previous->length;
            if (current.base < previous_end) return 0;
            if (current.base == previous_end && current.type == previous->type) {
                previous->length += current.length;
                continue;
            }
        }
        entries[normalized++] = current;
    }
    *count = normalized;
    return 1;
}

static int build_memory_map(range *memory, u32 memory_count,
                            range *reserved, u32 reserved_count,
                            range shim, range dtb, range *segments, u32 segment_count,
                            range executable_image,
                            range framebuffer, int has_framebuffer,
                            range *usable, u32 usable_count,
                            struct byter_memmap_entry *entries, u32 *entry_count) {
    *entry_count = 0;
    for (u32 m = 0; m < memory_count; ++m) {
        range ram = memory[m];
        u64 boundaries[MAX_MAP_BOUNDARIES];
        u32 boundary_count = 2;
        boundaries[0] = ram.base;
        boundaries[1] = ram.base + ram.length;
        for (u32 i = 0; i < reserved_count; ++i)
            if (!add_map_boundaries(boundaries, &boundary_count, ram, reserved[i])) return 0;
        if (!add_map_boundaries(boundaries, &boundary_count, ram, shim) ||
            !add_map_boundaries(boundaries, &boundary_count, ram, dtb)) return 0;
        for (u32 i = 0; i < segment_count; ++i)
            if (!add_map_boundaries(boundaries, &boundary_count, ram, segments[i])) return 0;
        if (!add_map_boundaries(boundaries, &boundary_count, ram, executable_image) ||
            !add_map_boundaries(boundaries, &boundary_count, ram, (range){0, 0x1000})) return 0;
        if (has_framebuffer && !add_map_boundaries(boundaries, &boundary_count, ram, framebuffer)) return 0;
        for (u32 i = 0; i < usable_count; ++i)
            if (!add_map_boundaries(boundaries, &boundary_count, ram, usable[i])) return 0;

        sort_boundaries(boundaries, boundary_count);
        u32 unique_count = 0;
        for (u32 i = 0; i < boundary_count; ++i)
            if (!unique_count || boundaries[i] != boundaries[unique_count - 1])
                boundaries[unique_count++] = boundaries[i];

        for (u32 i = 0; i + 1 < unique_count; ++i) {
            range piece = {boundaries[i], boundaries[i + 1] - boundaries[i]};
            if (!piece.length) continue;
            u64 type = BYTER_MEMMAP_RESERVED;
            for (u32 j = 0; j < usable_count; ++j)
                if (contains_range(usable[j], piece)) { type = BYTER_MEMMAP_USABLE; break; }
            for (u32 j = 0; j < reserved_count; ++j)
                if (contains_range(reserved[j], piece)) { type = BYTER_MEMMAP_RESERVED; break; }
            if (contains_range(shim, piece) || contains_range(dtb, piece))
                type = BYTER_MEMMAP_BOOTLOADER_RECLAIMABLE;
            for (u32 j = 0; j < segment_count; ++j)
                if (contains_range(segments[j], piece)) { type = BYTER_MEMMAP_EXECUTABLE_AND_MODULES; break; }
            if (contains_range(executable_image, piece)) type = BYTER_MEMMAP_EXECUTABLE_AND_MODULES;
            if (has_framebuffer && contains_range(framebuffer, piece))
                type = BYTER_MEMMAP_FRAMEBUFFER;
            if (contains_range((range){0, 0x1000}, piece)) type = BYTER_MEMMAP_RESERVED;
            if (!append_map_entry(entries, entry_count, piece, type)) return 0;
        }
    }

    range outside_reservations[MAX_OUT_RANGES];
    u32 outside_count = 0;
    for (u32 i = 0; i < reserved_count; ++i) {
        if (outside_count == MAX_OUT_RANGES) return 0;
        outside_reservations[outside_count++] = reserved[i];
    }
    for (u32 i = 0; i < memory_count; ++i)
        if (!subtract_range(outside_reservations, &outside_count, memory[i])) return 0;
    if (!normalize_ranges(outside_reservations, &outside_count)) return 0;
    for (u32 i = 0; i < outside_count; ++i)
        if (!append_map_entry(entries, entry_count, outside_reservations[i], BYTER_MEMMAP_RESERVED)) return 0;

    return *entry_count != 0 && sort_and_merge_map_entries(entries, entry_count);
}

static void copy_bytes(u8 *dst, const u8 *src, u64 length) {
    for (u64 i = 0; i < length; ++i) dst[i] = src[i];
}

static void zero_bytes(u8 *dst, u64 length) {
    for (u64 i = 0; i < length; ++i) dst[i] = 0;
}

static void flush_loaded_segment(range r, int executable) {
    u64 start = r.base & ~63ULL, end = r.base + r.length;
    for (u64 p = start; p < end; p += 64) __asm__ volatile("dc cvac, %0" :: "r"(p) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
    if (executable) {
        for (u64 p = start; p < end; p += 64) __asm__ volatile("ic ivau, %0" :: "r"(p) : "memory");
        __asm__ volatile("dsb sy; isb" ::: "memory");
    }
}

static int valid_elf(const u8 *file, u64 size, elf_header **header, elf_program **programs) {
    if (size < sizeof(elf_header)) return 0;
    elf_header *h = (elf_header *)file;
    if (h->ident[0] != 0x7f || h->ident[1] != 'E' || h->ident[2] != 'L' || h->ident[3] != 'F' ||
        h->ident[4] != 2 || h->ident[5] != 1 || h->ident[6] != 1 || h->type != 2 || h->machine != 183 ||
        h->version != 1 || h->ehsize != sizeof(elf_header) || h->phentsize != sizeof(elf_program) ||
        h->phnum == 0 || h->phnum > MAX_PHDRS || h->phoff > size ||
        (u64)h->phnum * sizeof(elf_program) > size - h->phoff) return 0;
    *header = h; *programs = (elf_program *)(file + h->phoff);
    return 1;
}

static const char byter_name[] = "Byter AArch64 shim";
static const char byter_version[] = "0.1";

static int has_id(const u64 *id, const u64 expected[4]) {
    return id[0] == expected[0] && id[1] == expected[1] &&
           id[2] == expected[2] && id[3] == expected[3];
}

static int matches_words(u64 address, const u64 *words, u32 count, u64 end) {
    if (address > end || (u64)count * sizeof(u64) > end - address) return 0;
    const u64 *candidate = (const u64 *)(u64)address;
    for (u32 i = 0; i < count; ++i)
        if (candidate[i] != words[i]) return 0;
    return 1;
}

static u64 hhdm_pointer(u64 physical, u64 offset) {
    return physical + offset;
}

static void find_request_window(range *segments, u32 count, u64 *window_start,
                                u64 *window_end) {
    const u64 start_marker[4] = BYTER_REQUESTS_START_MARKER;
    const u64 end_marker[2] = BYTER_REQUESTS_END_MARKER;
    u64 image_start = ~0ULL, image_end = 0;
    u64 last_start_marker = 0, first_end_marker = ~0ULL;
    int found_start_marker = 0, found_end_marker = 0;

    for (u32 i = 0; i < count; ++i) {
        range segment = segments[i];
        if (!segment.length || segment.base > ~0ULL - segment.length) continue;
        u64 end = segment.base + segment.length;
        if (segment.base < image_start) image_start = segment.base;
        if (end > image_end) image_end = end;

        if (segment.base > ~0ULL - 7) continue;
        u64 address = (segment.base + 7) & ~7ULL;
        while (address <= end && end - address >= sizeof(end_marker)) {
            if (matches_words(address, start_marker, 4, end)) {
                last_start_marker = address;
                found_start_marker = 1;
            }
            if (matches_words(address, end_marker, 2, end) &&
                (!found_end_marker || address < first_end_marker)) {
                first_end_marker = address;
                found_end_marker = 1;
            }
            if (end - address < sizeof(u64)) break;
            address += sizeof(u64);
        }
    }

    *window_start = found_start_marker ? last_start_marker + sizeof(start_marker) : image_start;
    *window_end = found_end_marker ? first_end_marker : image_end;
    /* Base revision 0 treats delimiters as hints, so ignore malformed bounds. */
    if (*window_start >= *window_end) {
        *window_start = image_start;
        *window_end = image_end;
    }
}

static int resolve_base_revision(range *segments, u32 count) {
    u64 window_start, window_end;
    find_request_window(segments, count, &window_start, &window_end);
    const u64 magic[2] = {0xf9562b2d5c95a6c8ULL, 0x6a7b384944536bdcULL};
    u64 *base_revision = 0;
    for (u32 i = 0; i < count; ++i) {
        range segment = segments[i];
        if (!segment.length || segment.base > ~0ULL - segment.length) continue;
        u64 end = segment.base + segment.length;
        u64 start = segment.base > window_start ? segment.base : window_start;
        if (start > ~0ULL - 7) continue;
        u64 address = (start + 7) & ~7ULL;
        while (address <= end && end - address >= 3 * sizeof(u64) &&
               address <= window_end && window_end - address >= 3 * sizeof(u64)) {
            u64 *candidate = (u64 *)(u64)address;
            if (candidate[0] == magic[0] && candidate[1] == magic[1]) {
                if (base_revision || candidate[2] != 0) return 0;
                base_revision = candidate;
            }
            address += sizeof(u64);
        }
    }
    if (base_revision) {
        base_revision[1] = 0; /* Base revision 0 was selected. */
        base_revision[2] = 0; /* The requested base revision is supported. */
    }
    return 1;
}

static void resolve_requests(range *segments, u32 count,
                             struct byter_bootloader_info_response *bootloader_info,
                             struct byter_executable_cmdline_response *cmdline,
                             struct byter_dtb_response *dtb,
                             struct byter_hhdm_response *hhdm,
                             struct byter_framebuffer_response *framebuffer,
                             struct byter_memmap_response *memmap,
                             struct byter_executable_address_response *executable_address,
                             u64 hhdm_offset) {
    u64 window_start, window_end;
    find_request_window(segments, count, &window_start, &window_end);
    if (window_start >= window_end) return;

    const u64 common_magic[2] = {0xc7b1dd30df4c8b88ULL, 0x0a82e883a194f07bULL};
    const u64 request_size = sizeof(struct byter_memmap_request);
    for (u32 i = 0; i < count; ++i) {
        range segment = segments[i];
        if (!segment.length || segment.base > ~0ULL - segment.length) continue;
        u64 segment_end = segment.base + segment.length;
        u64 start = segment.base > window_start ? segment.base : window_start;
        u64 end = segment_end < window_end ? segment_end : window_end;
        if (start > ~0ULL - 7) continue;
        u64 address = (start + 7) & ~7ULL;
        while (address <= end && end - address >= request_size) {
            u64 *id = (u64 *)(u64)address;
            if (id[0] != common_magic[0] || id[1] != common_magic[1]) {
                if (end - address < sizeof(u64)) break;
                address += sizeof(u64);
                continue;
            }

            u64 *revision = id + 4;
            u64 *response = id + 5;
            *response = 0;
            if (*revision == 0) {
                if (has_id(id, (const u64[])BYTER_BOOTLOADER_INFO_REQUEST_ID))
                    *response = hhdm_pointer((u64)bootloader_info, hhdm_offset);
                else if (has_id(id, (const u64[])BYTER_EXECUTABLE_CMDLINE_REQUEST_ID))
                    *response = hhdm_pointer((u64)cmdline, hhdm_offset);
                else if (has_id(id, (const u64[])BYTER_DTB_REQUEST_ID))
                    *response = hhdm_pointer((u64)dtb, hhdm_offset);
                else if (has_id(id, (const u64[])BYTER_HHDM_REQUEST_ID))
                    *response = hhdm_pointer((u64)hhdm, hhdm_offset);
                else if (has_id(id, (const u64[])BYTER_FRAMEBUFFER_REQUEST_ID))
                    *response = hhdm_pointer((u64)framebuffer, hhdm_offset);
                else if (has_id(id, (const u64[])BYTER_MEMMAP_REQUEST_ID))
                    *response = hhdm_pointer((u64)memmap, hhdm_offset);
                else if (has_id(id, (const u64[])BYTER_EXECUTABLE_ADDRESS_REQUEST_ID))
                    *response = hhdm_pointer((u64)executable_address, hhdm_offset);
            }
            if (end - address < sizeof(u64)) break;
            address += sizeof(u64);
        }
    }
}

/* Returns the ELF entry address, or zero on any validation/loading failure. */
u64 byter_shim_main(u8 *shim, u8 *dtb, u8 *elf, u64 elf_size, u64 shim_size,
                    u64 file_size, u8 *cmdline, u64 cmdline_size) {
    u32 dtb_size = 0, memory_count = 0, reserved_count = 0;
    u32 root_address_cells = 0, root_size_cells = 0;
    framebuffer_info framebuffer_data = {0};
    range memory[MAX_RANGES], reserved[MAX_RANGES];
    if ((u64)shim > ~0ULL - shim_size || file_size > shim_size) return 0;
    if (!cmdline || !cmdline_size || cmdline_size > MAX_CMDLINE_SIZE + 1 ||
        (u64)cmdline < (u64)shim || (u64)cmdline - (u64)shim > file_size ||
        cmdline_size > file_size - ((u64)cmdline - (u64)shim) ||
        cmdline[cmdline_size - 1] != 0) return 0;
    for (u64 i = 0; i + 1 < cmdline_size; ++i)
        if (cmdline[i] == 0) return 0;
    struct byter_framebuffer early_framebuffer;
    if (byter_a059_framebuffer_fallback(&early_framebuffer))
        set_status_framebuffer(&early_framebuffer);
    log_boot_stage("Starting AArch64 shim");
    int fdt_valid = parse_fdt(dtb, &dtb_size, memory, &memory_count, reserved,
                              &reserved_count, &framebuffer_data,
                              &root_address_cells, &root_size_cells);
    if (!fdt_valid || (u64)dtb > ~0ULL - dtb_size) return 0;
    if (!parse_fdt_device_ranges(dtb, dtb_size, root_address_cells,
                                 root_size_cells, reserved, &reserved_count)) return 0;
    if (!normalize_memory(memory, &memory_count)) return 0;
    range reserved_pages[MAX_RANGES];
    for (u32 i = 0; i < reserved_count; ++i)
        if (!page_aligned_range(reserved[i], &reserved_pages[i])) return 0;
    if (!normalize_ranges(reserved_pages, &reserved_count)) return 0;
    if (!framebuffer_data.format_valid) {
        struct byter_framebuffer fallback;
        if (byter_a059_framebuffer_fallback(&fallback)) {
            framebuffer_data = (framebuffer_info){
                .address = (u64)fallback.address,
                .size = fallback.pitch * fallback.height,
                .width = fallback.width,
                .height = fallback.height,
                .pitch = fallback.pitch,
                .red_size = fallback.red_mask_size,
                .red_shift = fallback.red_mask_shift,
                .green_size = fallback.green_mask_size,
                .green_shift = fallback.green_mask_shift,
                .blue_size = fallback.blue_mask_size,
                .blue_shift = fallback.blue_mask_shift,
                .format_valid = 1,
            };
        }
    }
    status_framebuffer = framebuffer_data;
    sync_status_console();
    log_boot_stage("Device tree and framebuffer ready");
    if ((u64)elf < (u64)shim || (u64)elf - (u64)shim > file_size ||
        elf_size > file_size - ((u64)elf - (u64)shim)) return 0;

    elf_header *eh;
    elf_program *ph;
    if (!valid_elf(elf, elf_size, &eh, &ph)) return 0;
    range segments[MAX_PHDRS], virtual_segments[MAX_PHDRS];
    range physical_page_ranges[MAX_PHDRS], virtual_page_ranges[MAX_PHDRS];
    struct byter_aarch64_load_segment page_table_segments[MAX_PHDRS];
    u32 segment_count = 0;
    u64 low = ~0ULL, high = 0, virtual_low = ~0ULL, total_mem = 0;
    u64 load_bias = 0;
    int entry_ok = 0;
    for (u32 i = 0; i < eh->phnum; ++i) {
        elf_program *p = &ph[i];
        if (p->type != 1 || !p->memsz) continue;
        if (p->filesz > p->memsz || p->offset > elf_size || p->filesz > elf_size - p->offset ||
            p->paddr > ~0ULL - p->memsz || p->vaddr < p->paddr || p->vaddr > ~0ULL - p->memsz ||
            p->memsz > MAX_ELF_MEMORY - total_mem ||
            (p->align > 1 && ((p->align & (p->align - 1)) || ((p->vaddr - p->offset) & (p->align - 1))))) return 0;
        range r = {p->paddr, p->memsz}, vr = {p->vaddr, p->memsz};
        range physical_page, virtual_page;
        if ((p->paddr & 0xfff) != (p->vaddr & 0xfff) ||
            !page_aligned_range(r, &physical_page) || !page_aligned_range(vr, &virtual_page) ||
            p->vaddr < 0xffffffff80000000ULL || !in_memory(memory, memory_count, r) ||
            !in_memory(memory, memory_count, (range){(u64)shim, shim_size}) ||
            !in_memory(memory, memory_count, (range){(u64)dtb, dtb_size}) ||
            overlaps(r, (range){(u64)shim, shim_size}) ||
            overlaps(r, (range){(u64)dtb, dtb_size})) return 0;
        u64 segment_bias = p->vaddr - p->paddr;
        if (segment_count && segment_bias != load_bias) return 0;
        load_bias = segment_bias;
        for (u32 j = 0; j < reserved_count; ++j)
            if (overlaps(physical_page, reserved_pages[j])) return 0;
        for (u32 j = 0; j < segment_count; ++j)
            if (overlaps(physical_page, physical_page_ranges[j]) ||
                overlaps(virtual_page, virtual_page_ranges[j])) return 0;
        segments[segment_count] = r;
        virtual_segments[segment_count] = vr;
        physical_page_ranges[segment_count] = physical_page;
        virtual_page_ranges[segment_count] = virtual_page;
        page_table_segments[segment_count] = (struct byter_aarch64_load_segment){
            .physical_base = p->paddr, .virtual_base = p->vaddr,
            .length = p->memsz, .flags = p->flags
        };
        ++segment_count;
        total_mem += p->memsz;
        if (r.base < low) low = r.base;
        if (r.base + r.length > high) high = r.base + r.length;
        if (vr.base < virtual_low) virtual_low = vr.base;
        if ((p->flags & 1) && eh->entry >= vr.base && eh->entry - vr.base < vr.length) entry_ok = 1;
    }
    if (!segment_count || !entry_ok || high <= low) return 0;
    log_boot_stage("Kernel ELF validated");

    u64 image_low = ~0ULL, image_high = 0, image_size = 0;
    for (u32 i = 0; i < segment_count; ++i) {
        if (physical_page_ranges[i].base < image_low) image_low = physical_page_ranges[i].base;
        if (physical_page_ranges[i].base + physical_page_ranges[i].length > image_high)
            image_high = physical_page_ranges[i].base + physical_page_ranges[i].length;
        if (physical_page_ranges[i].length > ~0ULL - image_size) return 0;
        image_size += physical_page_ranges[i].length;
    }
    if (image_high <= image_low || image_size != image_high - image_low) return 0;
    log_boot_stage("Kernel segments are contiguous");
    range executable_image = {image_low, image_size};
    if (!in_memory(memory, memory_count, executable_image)) return 0;
    if (overlaps(executable_image, (range){(u64)shim, shim_size}) ||
        overlaps(executable_image, (range){(u64)dtb, dtb_size})) return 0;
    log_boot_stage("Kernel image is in RAM");
    for (u32 i = 0; i < reserved_count; ++i)
        if (overlaps(executable_image, reserved_pages[i])) return 0;
    log_boot_stage("Kernel avoids reserved memory");

    range framebuffer_range = {framebuffer_data.address, framebuffer_data.size};
    int has_framebuffer = framebuffer_data.format_valid && framebuffer_range.length &&
        framebuffer_range.base <= ~0ULL - framebuffer_range.length;
    if (has_framebuffer && (overlaps(framebuffer_range, (range){(u64)shim, shim_size}) ||
        overlaps(framebuffer_range, (range){(u64)dtb, dtb_size}))) return 0;
    if (has_framebuffer && overlaps(framebuffer_range, executable_image)) return 0;

    range shim_pages, dtb_pages;
    if (!page_aligned_range((range){(u64)shim, shim_size}, &shim_pages) ||
        !page_aligned_range((range){(u64)dtb, dtb_size}, &dtb_pages)) return 0;
    range framebuffer_pages = {0};
    if (has_framebuffer && !page_aligned_range(framebuffer_range, &framebuffer_pages)) return 0;
    if (has_framebuffer && (overlaps(framebuffer_pages, shim_pages) ||
        overlaps(framebuffer_pages, dtb_pages) || overlaps(framebuffer_pages, executable_image))) return 0;
    log_boot_stage("Kernel avoids framebuffer memory");

    range usable[MAX_OUT_RANGES]; u32 usable_count = 0;
    for (u32 i = 0; i < memory_count; ++i) {
        range aligned;
        if (page_aligned_inward_range(memory[i], &aligned)) {
            if (usable_count == MAX_OUT_RANGES) return 0;
            usable[usable_count++] = aligned;
        }
    }
    for (u32 i = 0; i < reserved_count; ++i)
        if (!subtract_range(usable, &usable_count, reserved_pages[i])) return 0;
    if (!subtract_range(usable, &usable_count, shim_pages) ||
        !subtract_range(usable, &usable_count, dtb_pages)) return 0;
    if (!subtract_range(usable, &usable_count, executable_image) ||
        !subtract_range(usable, &usable_count, (range){0, 0x1000})) return 0;
    if (has_framebuffer && !subtract_range(usable, &usable_count, framebuffer_pages)) return 0;
    log_boot_stage("Usable memory ranges prepared");

    if (shim_size < HANDOFF_OFFSET + HANDOFF_SIZE + STACK_SIZE + 0x10000) return 0;
    u8 *handoff = shim + shim_size - HANDOFF_OFFSET;
    struct byter_bootloader_info_response *bootloader_info = (void *)handoff;
    struct byter_executable_cmdline_response *cmdline_response = (void *)(bootloader_info + 1);
    struct byter_dtb_response *dtb_response = (void *)(cmdline_response + 1);
    struct byter_framebuffer_response *framebuffer_response = (void *)(dtb_response + 1);
    struct byter_hhdm_response *hhdm_response = (void *)(framebuffer_response + 1);
    struct byter_memmap_response *memmap_response = (void *)(hhdm_response + 1);
    struct byter_executable_address_response *executable_address_response = (void *)(memmap_response + 1);
    struct byter_framebuffer *framebuffer = (void *)(executable_address_response + 1);
    struct byter_framebuffer **framebuffers = (void *)(framebuffer + 1);
    struct byter_memmap_entry *memmap_entries = (void *)(framebuffers + 1);
    struct byter_memmap_entry **memmap_entry_pointers = (void *)(memmap_entries + MAX_OUT_RANGES);
    if ((u8 *)(memmap_entry_pointers + MAX_OUT_RANGES) > handoff + HANDOFF_SIZE) return 0;
    u32 memmap_count = 0;
    if (!build_memory_map(memory, memory_count, reserved_pages, reserved_count,
                          shim_pages, dtb_pages,
                          segments, segment_count, executable_image, framebuffer_pages, has_framebuffer,
                          usable, usable_count, memmap_entries, &memmap_count)) return 0;
    log_boot_stage("Memory map built");
    struct byter_aarch64_range page_table_memory[MAX_RANGES];
    struct byter_aarch64_range page_table_reserved[MAX_RANGES];
    for (u32 i = 0; i < memory_count; ++i)
        page_table_memory[i] = (struct byter_aarch64_range){memory[i].base, memory[i].length};
    for (u32 i = 0; i < reserved_count; ++i)
        page_table_reserved[i] = (struct byter_aarch64_range){
            reserved_pages[i].base, reserved_pages[i].length
        };
    struct byter_aarch64_page_tables page_tables;
    u32 page_table_failure_stage;
    if (!byter_aarch64_build_page_tables(shim, shim_size, page_table_memory, memory_count,
            page_table_reserved, reserved_count,
            (struct byter_aarch64_range){framebuffer_range.base, framebuffer_range.length},
            has_framebuffer, page_table_segments, segment_count, &page_tables, &page_table_failure_stage)) {
        static const char *const failure_messages[] = {
            "", "ERROR: page table initialization failed", "ERROR: invalid mapping ranges",
            "ERROR: low identity map failed", "ERROR: RAM identity map failed",
            "ERROR: shim identity map failed", "ERROR: framebuffer identity map failed",
            "ERROR: low HHDM map failed", "ERROR: RAM HHDM map failed",
            "ERROR: framebuffer HHDM map failed", "ERROR: kernel map failed",
            "ERROR: page table cache clean failed"
        };
        if (page_table_failure_stage < sizeof(failure_messages) / sizeof(failure_messages[0]))
            log_boot_stage(failure_messages[page_table_failure_stage]);
        else
            log_boot_stage("ERROR: page table setup failed");
        return 0;
    }
    u64 hhdm_offset = page_tables.hhdm_offset;
    log_boot_stage("Page tables ready");

    for (u32 i = 0; i < memmap_count; ++i)
        memmap_entry_pointers[i] = (struct byter_memmap_entry *)(u64)
            hhdm_pointer((u64)&memmap_entries[i], hhdm_offset);
    *memmap_response = (struct byter_memmap_response){
        .revision = 0, .entry_count = memmap_count,
        .entries = (struct byter_memmap_entry **)(u64)
            hhdm_pointer((u64)memmap_entry_pointers, hhdm_offset)
    };
    *executable_address_response = (struct byter_executable_address_response){
        .revision = 0, .physical_base = low, .virtual_base = virtual_low
    };
    *bootloader_info = (struct byter_bootloader_info_response){
        .revision = 0,
        .name = (char *)(u64)hhdm_pointer((u64)byter_name, hhdm_offset),
        .version = (char *)(u64)hhdm_pointer((u64)byter_version, hhdm_offset)
    };
    cmdline_response->revision = 0;
    cmdline_response->cmdline = (char *)(u64)hhdm_pointer((u64)cmdline, hhdm_offset);
    *dtb_response = (struct byter_dtb_response){
        .revision = 0,
        .dtb_ptr = (void *)(u64)hhdm_pointer((u64)dtb, hhdm_offset)
    };
    *hhdm_response = (struct byter_hhdm_response){.revision = 0, .offset = hhdm_offset};
    struct byter_framebuffer_response *framebuffer_result = 0;
    if (has_framebuffer) {
        *framebuffer = (struct byter_framebuffer){
            .address = (void *)(u64)hhdm_pointer(framebuffer_data.address, hhdm_offset),
            .width = framebuffer_data.width, .height = framebuffer_data.height,
            .pitch = framebuffer_data.pitch, .bpp = 32,
            .memory_model = BYTER_FRAMEBUFFER_RGB,
            .red_mask_size = framebuffer_data.red_size,
            .red_mask_shift = framebuffer_data.red_shift,
            .green_mask_size = framebuffer_data.green_size,
            .green_mask_shift = framebuffer_data.green_shift,
            .blue_mask_size = framebuffer_data.blue_size,
            .blue_mask_shift = framebuffer_data.blue_shift,
        };
        framebuffers[0] = (struct byter_framebuffer *)(u64)
            hhdm_pointer((u64)framebuffer, hhdm_offset);
        *framebuffer_response = (struct byter_framebuffer_response){
            .revision = 0, .framebuffer_count = 1,
            .framebuffers = (struct byter_framebuffer **)(u64)
                hhdm_pointer((u64)framebuffers, hhdm_offset)
        };
        framebuffer_result = framebuffer_response;
    }
    for (u32 i = 0; i < eh->phnum; ++i) {
        elf_program *p = &ph[i];
        if (p->type != 1 || !p->memsz) continue;
        u8 *dst = (u8 *)(u64)p->paddr;
        copy_bytes(dst, elf + p->offset, p->filesz);
        zero_bytes(dst + p->filesz, p->memsz - p->filesz);
        flush_loaded_segment((range){p->paddr, p->memsz}, !!(p->flags & 1));
    }
    log_boot_stage("Kernel ELF loaded");

    if (!resolve_base_revision(segments, segment_count)) {
        log_boot_stage("ERROR: unsupported base revision request");
        return 0;
    }
    log_boot_stage("Byter base revision accepted");

    resolve_requests(segments, segment_count, bootloader_info, cmdline_response, dtb_response,
                     hhdm_response, framebuffer_result, memmap_response,
                     executable_address_response, hhdm_offset);
    log_boot_stage("Boot protocol responses ready");
    boot_context *context = (boot_context *)(handoff + HANDOFF_SIZE - sizeof(*context));
    *context = (boot_context){eh->entry, page_tables.ttbr0, page_tables.ttbr1, page_tables.tcr};
    return (u64)context;
}
