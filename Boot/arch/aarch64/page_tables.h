#ifndef BYTER_AARCH64_PAGE_TABLES_H
#define BYTER_AARCH64_PAGE_TABLES_H

#include <stdint.h>

#define BYTER_AARCH64_HHDM_OFFSET 0xffff800000000000ULL

struct byter_aarch64_range {
    uint64_t base;
    uint64_t length;
};

struct byter_aarch64_load_segment {
    uint64_t physical_base;
    uint64_t virtual_base;
    uint64_t length;
    uint32_t flags;
};

struct byter_aarch64_page_tables {
    uint64_t ttbr0;
    uint64_t ttbr1;
    uint64_t tcr;
    uint64_t hhdm_offset;
};

enum byter_aarch64_page_tables_stage {
    BYTER_PT_STAGE_INIT = 1,
    BYTER_PT_STAGE_RANGES,
    BYTER_PT_STAGE_IDENTITY_LOW,
    BYTER_PT_STAGE_IDENTITY_RAM,
    BYTER_PT_STAGE_IDENTITY_SHIM,
    BYTER_PT_STAGE_IDENTITY_FRAMEBUFFER,
    BYTER_PT_STAGE_HHDM_LOW,
    BYTER_PT_STAGE_HHDM_RAM,
    BYTER_PT_STAGE_HHDM_FRAMEBUFFER,
    BYTER_PT_STAGE_KERNEL,
    BYTER_PT_STAGE_CLEAN,
};

int byter_aarch64_build_page_tables(
    uint8_t *shim, uint64_t shim_size,
    const struct byter_aarch64_range *memory, uint32_t memory_count,
    const struct byter_aarch64_range *reserved, uint32_t reserved_count,
    struct byter_aarch64_range framebuffer, int has_framebuffer,
    const struct byter_aarch64_load_segment *segments, uint32_t segment_count,
    struct byter_aarch64_page_tables *result, uint32_t *failure_stage);

#endif
