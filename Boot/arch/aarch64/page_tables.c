#include "page_tables.h"

#define PAGE_SIZE 0x1000ULL
#define BLOCK_SIZE 0x200000ULL
#define BASE_REVISION_ZERO_IDENTITY_SIZE 0x100000000ULL
#define TABLE_POOL_OFFSET 0x300000ULL
#define TABLE_POOL_SIZE 0x200000ULL
#define MAX_MAP_RANGES 512
#define MAX_PA_BITS 48
#define PA_MASK 0x0000fffffffff000ULL
#define DESC_VALID 1ULL
#define DESC_TABLE 3ULL
#define DESC_BLOCK 1ULL
#define ATTR_NORMAL_WB 0ULL
#define ATTR_NORMAL_NC 1ULL
#define PTE_AP_RO (2ULL << 6)
#define PTE_SH_INNER (3ULL << 8)
#define PTE_AF (1ULL << 10)
#define PTE_PXN (1ULL << 53)
#define PTE_UXN (1ULL << 54)
#define PF_X 1U
#define PF_W 2U

typedef struct {
    uint64_t base;
    uint64_t length;
} range;

typedef struct {
    uint64_t *pool;
    uint64_t used_pages;
    uint64_t max_pages;
    uint64_t *ttbr0;
    uint64_t *ttbr1;
    uint64_t max_physical_address;
    uint8_t pa_range;
} page_table_state;

static int overlaps(range a, range b) {
    return a.base < b.base + b.length && b.base < a.base + a.length;
}

static int subtract_range(range *ranges, uint32_t *count, range cut) {
    if (!cut.length || cut.base > ~0ULL - cut.length) return 0;
    for (uint32_t i = 0; i < *count;) {
        range current = ranges[i];
        if (!overlaps(current, cut)) {
            ++i;
            continue;
        }
        uint64_t current_end = current.base + current.length;
        uint64_t cut_end = cut.base + cut.length;
        if (cut.base <= current.base && cut_end >= current_end) {
            ranges[i] = ranges[--(*count)];
        } else if (cut.base <= current.base) {
            ranges[i].base = cut_end;
            ranges[i].length = current_end - cut_end;
            ++i;
        } else if (cut_end >= current_end) {
            ranges[i].length = cut.base - current.base;
            ++i;
        } else {
            if (*count == MAX_MAP_RANGES) return 0;
            ranges[*count] = (range){cut_end, current_end - cut_end};
            ranges[i].length = cut.base - current.base;
            ++(*count);
            ++i;
        }
    }
    return 1;
}

static int align_range(range input, range *output) {
    if (!input.length || input.base > ~0ULL - input.length) return 0;
    uint64_t end = input.base + input.length;
    uint64_t aligned_end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (end > ~0ULL - (PAGE_SIZE - 1) || aligned_end < end) return 0;
    output->base = input.base & ~(PAGE_SIZE - 1);
    output->length = aligned_end - output->base;
    return output->length != 0;
}

static uint64_t *allocate_table(page_table_state *state) {
    if (state->used_pages == state->max_pages) return 0;
    uint64_t *table = state->pool + state->used_pages * (PAGE_SIZE / sizeof(uint64_t));
    ++state->used_pages;
    for (uint32_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); ++i) table[i] = 0;
    return table;
}

static uint64_t *child_table(page_table_state *state, uint64_t *parent, uint32_t index) {
    uint64_t descriptor = parent[index];
    if (descriptor & DESC_VALID) {
        if ((descriptor & DESC_TABLE) != DESC_TABLE) return 0;
        return (uint64_t *)(descriptor & PA_MASK);
    }
    uint64_t *child = allocate_table(state);
    if (!child) return 0;
    parent[index] = ((uint64_t)child & PA_MASK) | DESC_TABLE;
    return child;
}

static uint64_t *level2_table(page_table_state *state, uint64_t address) {
    uint64_t *root = (address >> 63) ? state->ttbr1 : state->ttbr0;
    uint32_t l0 = (uint32_t)((address >> 39) & 0x1ff);
    uint32_t l1 = (uint32_t)((address >> 30) & 0x1ff);
    uint64_t *table1 = child_table(state, root, l0);
    if (!table1) return 0;
    return child_table(state, table1, l1);
}

static uint64_t page_attributes(uint64_t attr_index, uint32_t flags) {
    uint64_t attributes = (attr_index << 2) | PTE_SH_INNER | PTE_AF;
    if (!(flags & PF_W)) attributes |= PTE_AP_RO;
    if (!(flags & PF_X)) attributes |= PTE_PXN | PTE_UXN;
    else attributes |= PTE_UXN;
    return attributes;
}

static int map_page(page_table_state *state, uint64_t virtual_address,
                    uint64_t physical_address, uint64_t attributes) {
    uint64_t *l2 = level2_table(state, virtual_address);
    if (!l2) return 0;
    uint32_t l2_index = (uint32_t)((virtual_address >> 21) & 0x1ff);
    uint64_t *l3 = child_table(state, l2, l2_index);
    if (!l3) return 0;
    uint32_t l3_index = (uint32_t)((virtual_address >> 12) & 0x1ff);
    uint64_t descriptor = (physical_address & PA_MASK) | attributes | DESC_TABLE;
    if (l3[l3_index] && l3[l3_index] != descriptor) return 0;
    l3[l3_index] = descriptor;
    return 1;
}

static int map_ram_range(page_table_state *state, range region, uint64_t virtual_offset) {
    range aligned;
    if (!align_range(region, &aligned) || aligned.base > state->max_physical_address ||
        aligned.length > state->max_physical_address - aligned.base ||
        virtual_offset > ~0ULL - aligned.base ||
        (virtual_offset && virtual_offset + aligned.base < 0xffff000000000000ULL) ||
        (!virtual_offset && (aligned.base >= 0x800000000000ULL ||
         aligned.length > 0x800000000000ULL - aligned.base)) ||
        aligned.length - 1 > ~0ULL - (virtual_offset + aligned.base)) return 0;
    uint64_t address = aligned.base;
    uint64_t end = address + aligned.length;
    uint64_t attributes = page_attributes(ATTR_NORMAL_WB, PF_W | PF_X);
    while (address < end) {
        uint64_t remaining = end - address;
        uint64_t virtual_address = virtual_offset + address;
        if (!(address & (BLOCK_SIZE - 1)) && !(virtual_address & (BLOCK_SIZE - 1)) &&
            remaining >= BLOCK_SIZE) {
            uint64_t *l2 = level2_table(state, virtual_address);
            if (!l2) return 0;
            uint32_t index = (uint32_t)((virtual_address >> 21) & 0x1ff);
            uint64_t descriptor = address | attributes | DESC_BLOCK;
            if (l2[index] && l2[index] != descriptor) return 0;
            l2[index] = descriptor;
            address += BLOCK_SIZE;
        } else {
            if (!map_page(state, virtual_address, address, attributes)) return 0;
            address += PAGE_SIZE;
        }
    }
    return 1;
}

static int map_reserved_above_4g(page_table_state *state,
                                 const struct byter_aarch64_range *memory,
                                 uint32_t memory_count,
                                 const struct byter_aarch64_range *reserved,
                                 uint32_t reserved_count, range framebuffer_pages,
                                 uint64_t virtual_offset) {
    for (uint32_t i = 0; i < reserved_count; ++i) {
        range aligned;
        if (!align_range((range){reserved[i].base, reserved[i].length}, &aligned)) return 0;
        range pieces[MAX_MAP_RANGES] = {aligned};
        uint32_t count = 1;
        for (uint32_t j = 0; j < memory_count; ++j) {
            range ram;
            if (!align_range((range){memory[j].base, memory[j].length}, &ram) ||
                !subtract_range(pieces, &count, ram)) return 0;
        }
        if (!subtract_range(pieces, &count, (range){0, BASE_REVISION_ZERO_IDENTITY_SIZE}) ||
            (framebuffer_pages.length &&
             !subtract_range(pieces, &count, framebuffer_pages))) return 0;
        for (uint32_t j = 0; j < count; ++j)
            if (!map_ram_range(state, pieces[j], virtual_offset)) return 0;
    }
    return 1;
}

static int map_special_range(page_table_state *state, range region, uint64_t attr_index,
                             uint32_t flags, uint64_t virtual_offset) {
    range aligned;
    if (!align_range(region, &aligned) || aligned.base > state->max_physical_address ||
        aligned.length > state->max_physical_address - aligned.base) return 0;
    uint64_t attributes = page_attributes(attr_index, flags);
    if (virtual_offset > ~0ULL - aligned.base ||
        (virtual_offset && virtual_offset + aligned.base < 0xffff000000000000ULL) ||
        (!virtual_offset && (aligned.base >= 0x800000000000ULL ||
         aligned.length > 0x800000000000ULL - aligned.base)) ||
        aligned.length - 1 > ~0ULL - (virtual_offset + aligned.base)) return 0;
    for (uint64_t offset = 0; offset < aligned.length; offset += PAGE_SIZE)
        if (!map_page(state, virtual_offset + aligned.base + offset,
                      aligned.base + offset, attributes)) return 0;
    return 1;
}

static int map_kernel_segment(page_table_state *state,
                              const struct byter_aarch64_load_segment *segment) {
    range physical, virtual;
    if (!align_range((range){segment->physical_base, segment->length}, &physical) ||
        !align_range((range){segment->virtual_base, segment->length}, &virtual) ||
        (segment->physical_base & (PAGE_SIZE - 1)) != (segment->virtual_base & (PAGE_SIZE - 1)) ||
        physical.length != virtual.length ||
        physical.base > state->max_physical_address ||
        physical.length > state->max_physical_address - physical.base ||
        segment->virtual_base < 0xffffffff80000000ULL)
        return 0;

    uint64_t attributes = page_attributes(ATTR_NORMAL_WB, segment->flags);
    for (uint64_t offset = 0; offset < physical.length; offset += PAGE_SIZE) {
        if (!map_page(state, virtual.base + offset, physical.base + offset, attributes)) return 0;
    }
    return 1;
}

static int physical_size(uint64_t *max_address, uint8_t *pa_range, uint32_t *pa_bits) {
    uint64_t feature;
    __asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(feature));
    uint32_t range_encoding = (uint32_t)(feature & 0xf);
    uint32_t granule_4k = (uint32_t)((feature >> 28) & 0xf);
    static const uint8_t bits_by_encoding[8] = {32, 36, 40, 42, 44, 48, 52, 52};
    if (range_encoding >= 8 || granule_4k == 0xf) return 0;
    *pa_range = (uint8_t)(range_encoding > 5 ? 5 : range_encoding);
    *pa_bits = bits_by_encoding[range_encoding];
    if (*pa_bits > MAX_PA_BITS) *pa_bits = MAX_PA_BITS;
    if (*pa_range < range_encoding) *pa_bits = MAX_PA_BITS;
    *max_address = 1ULL << *pa_bits;
    return 1;
}

int byter_aarch64_build_page_tables(
    uint8_t *shim, uint64_t shim_size,
    const struct byter_aarch64_range *memory, uint32_t memory_count,
    const struct byter_aarch64_range *reserved, uint32_t reserved_count,
    struct byter_aarch64_range framebuffer, int has_framebuffer,
    const struct byter_aarch64_load_segment *segments, uint32_t segment_count,
    struct byter_aarch64_page_tables *result, uint32_t *failure_stage) {
    if (failure_stage) *failure_stage = BYTER_PT_STAGE_INIT;
    if (!shim || !memory || !memory_count || (!reserved && reserved_count) ||
        !segments || !segment_count || !result ||
        !failure_stage || shim_size <= TABLE_POOL_OFFSET || shim_size < 0x400000ULL) return 0;

    page_table_state state = {0};
    uint32_t pa_bits;
    if (!physical_size(&state.max_physical_address, &state.pa_range, &pa_bits)) return 0;
    uint64_t pool_base = (uint64_t)shim + shim_size - TABLE_POOL_OFFSET;
    if (pool_base > state.max_physical_address || TABLE_POOL_SIZE > state.max_physical_address - pool_base)
        return 0;
    state.pool = (uint64_t *)pool_base;
    state.max_pages = TABLE_POOL_SIZE / PAGE_SIZE;
    state.ttbr0 = allocate_table(&state);
    state.ttbr1 = allocate_table(&state);
    if (!state.ttbr0 || !state.ttbr1) return 0;

    *failure_stage = BYTER_PT_STAGE_RANGES;
    range framebuffer_pages = {0};
    range shim_pages;
    if (!align_range((range){(uint64_t)shim, shim_size}, &shim_pages)) return 0;
    if (has_framebuffer && !align_range((range){framebuffer.base, framebuffer.length}, &framebuffer_pages))
        return 0;

    range identity_ranges[MAX_MAP_RANGES];
    uint32_t identity_count = 0;
    for (uint32_t i = 0; i < memory_count; ++i) {
        range aligned;
        if (!align_range((range){memory[i].base, memory[i].length}, &aligned) ||
            identity_count == MAX_MAP_RANGES) return 0;
        identity_ranges[identity_count++] = aligned;
    }
    range low_identity_ranges[3] = {{PAGE_SIZE, BASE_REVISION_ZERO_IDENTITY_SIZE - PAGE_SIZE}};
    uint32_t low_identity_count = 1;
    if (!subtract_range(identity_ranges, &identity_count, (range){0, BASE_REVISION_ZERO_IDENTITY_SIZE}) ||
        !subtract_range(identity_ranges, &identity_count, shim_pages) ||
        (has_framebuffer && !subtract_range(identity_ranges, &identity_count, framebuffer_pages)) ||
        !subtract_range(low_identity_ranges, &low_identity_count, shim_pages) ||
        (has_framebuffer && !subtract_range(low_identity_ranges, &low_identity_count, framebuffer_pages)))
        return 0;

    *failure_stage = BYTER_PT_STAGE_IDENTITY_LOW;
    for (uint32_t i = 0; i < low_identity_count; ++i)
        if (!map_ram_range(&state, low_identity_ranges[i], 0)) return 0;
    *failure_stage = BYTER_PT_STAGE_IDENTITY_RAM;
    for (uint32_t i = 0; i < identity_count; ++i)
        if (!map_ram_range(&state, identity_ranges[i], 0)) return 0;
    if (!map_reserved_above_4g(&state, memory, memory_count, reserved, reserved_count,
                               framebuffer_pages, 0)) return 0;
    *failure_stage = BYTER_PT_STAGE_IDENTITY_SHIM;
    if (!map_special_range(&state, (range){(uint64_t)shim, shim_size}, ATTR_NORMAL_WB,
                           PF_W | PF_X, 0)) return 0;
    *failure_stage = BYTER_PT_STAGE_IDENTITY_FRAMEBUFFER;
    if (has_framebuffer && !map_special_range(&state,
        (range){framebuffer_pages.base, framebuffer_pages.length}, ATTR_NORMAL_NC, PF_W | PF_X, 0)) return 0;

    range hhdm_ranges[MAX_MAP_RANGES];
    range low_hhdm_ranges[2] = {{0, BASE_REVISION_ZERO_IDENTITY_SIZE}};
    uint32_t low_hhdm_count = 1;
    uint32_t hhdm_count = 0;
    for (uint32_t i = 0; i < memory_count; ++i) {
        range aligned;
        if (!align_range((range){memory[i].base, memory[i].length}, &aligned) ||
            hhdm_count == MAX_MAP_RANGES) return 0;
        hhdm_ranges[hhdm_count++] = aligned;
    }
    if (!subtract_range(hhdm_ranges, &hhdm_count,
                        (range){0, BASE_REVISION_ZERO_IDENTITY_SIZE}) ||
        (has_framebuffer && !subtract_range(hhdm_ranges, &hhdm_count, framebuffer_pages)) ||
        (has_framebuffer && !subtract_range(low_hhdm_ranges, &low_hhdm_count, framebuffer_pages)))
        return 0;
    *failure_stage = BYTER_PT_STAGE_HHDM_LOW;
    for (uint32_t i = 0; i < low_hhdm_count; ++i)
        if (!map_ram_range(&state, low_hhdm_ranges[i], BYTER_AARCH64_HHDM_OFFSET)) return 0;
    *failure_stage = BYTER_PT_STAGE_HHDM_RAM;
    for (uint32_t i = 0; i < hhdm_count; ++i)
        if (!map_ram_range(&state, hhdm_ranges[i], BYTER_AARCH64_HHDM_OFFSET)) return 0;
    if (!map_reserved_above_4g(&state, memory, memory_count, reserved, reserved_count,
                               framebuffer_pages, BYTER_AARCH64_HHDM_OFFSET)) return 0;
    *failure_stage = BYTER_PT_STAGE_HHDM_FRAMEBUFFER;
    if (has_framebuffer && !map_special_range(&state,
        (range){framebuffer_pages.base, framebuffer_pages.length}, ATTR_NORMAL_NC, PF_W | PF_X,
        BYTER_AARCH64_HHDM_OFFSET)) return 0;
    *failure_stage = BYTER_PT_STAGE_KERNEL;
    for (uint32_t i = 0; i < segment_count; ++i)
        if (!map_kernel_segment(&state, &segments[i])) return 0;

    *failure_stage = BYTER_PT_STAGE_CLEAN;
    uint64_t tcr = 16ULL | (1ULL << 8) | (1ULL << 10) | (2ULL << 12) |
                   (16ULL << 16) | (1ULL << 24) | (1ULL << 26) |
                   (2ULL << 28) | (2ULL << 30) | ((uint64_t)state.pa_range << 32);
    for (uint64_t offset = 0; offset < state.used_pages * PAGE_SIZE; offset += 64)
        __asm__ volatile("dc cvac, %0" :: "r"(pool_base + offset) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");

    *result = (struct byter_aarch64_page_tables){
        .ttbr0 = (uint64_t)state.ttbr0,
        .ttbr1 = (uint64_t)state.ttbr1,
        .tcr = tcr,
        .hhdm_offset = BYTER_AARCH64_HHDM_OFFSET,
    };
    (void)pa_bits;
    return 1;
}
