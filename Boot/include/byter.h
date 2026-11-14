/* SPDX-License-Identifier: 0BSD */

/* Copyright (C) 2022-2026 Mintsuki and contributors.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef BYTER_H
#define BYTER_H 1

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Misc */

#ifdef BYTER_NO_POINTERS
#  define BYTER_PTR(TYPE) uint64_t
#else
#  define BYTER_PTR(TYPE) TYPE
#endif

#define BYTER_REQUESTS_START_MARKER { 0xf6b8f4b39de7d1ae, 0xfab91a6940fcb9cf, \
                                       0x785c6ed015d3e316, 0x181e920a7852b9d9 }
#define BYTER_REQUESTS_END_MARKER { 0xadc0e0531bb10d03, 0x9572709f31764c62 }

#define BYTER_BASE_REVISION(N) { 0xf9562b2d5c95a6c8, 0x6a7b384944536bdc, (N) }

#define BYTER_BASE_REVISION_SUPPORTED(VAR) ((VAR)[2] == 0)

#define BYTER_LOADED_BASE_REVISION_VALID(VAR) ((VAR)[1] != 0x6a7b384944536bdc)
#define BYTER_LOADED_BASE_REVISION(VAR) ((VAR)[1])

#define BYTER_COMMON_MAGIC 0xc7b1dd30df4c8b88, 0x0a82e883a194f07b

struct byter_uuid {
    uint32_t a;
    uint16_t b;
    uint16_t c;
    uint8_t d[8];
};

#define BYTER_MEDIA_TYPE_GENERIC 0
#define BYTER_MEDIA_TYPE_OPTICAL 1
#define BYTER_MEDIA_TYPE_TFTP 2

struct byter_file {
    uint64_t revision;
    BYTER_PTR(void *) address;
    uint64_t size;
    BYTER_PTR(char *) path;
    BYTER_PTR(char *) string;
    uint32_t media_type;
    uint32_t unused;
    uint8_t tftp_ipv4[4];
    uint32_t tftp_port;
    uint32_t partition_index;
    uint32_t mbr_disk_id;
    struct byter_uuid gpt_disk_uuid;
    struct byter_uuid gpt_part_uuid;
    struct byter_uuid part_uuid;
};

/* Boot info */

#define BYTER_BOOTLOADER_INFO_REQUEST_ID { BYTER_COMMON_MAGIC, 0xf55038d8e2a1202f, 0x279426fcf5f59740 }

struct byter_bootloader_info_response {
    uint64_t revision;
    BYTER_PTR(char *) name;
    BYTER_PTR(char *) version;
};

struct byter_bootloader_info_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_bootloader_info_response *) response;
};

/* Executable command line */

#define BYTER_EXECUTABLE_CMDLINE_REQUEST_ID { BYTER_COMMON_MAGIC, 0x4b161536e598651e, 0xb390ad4a2f1f303a }

struct byter_executable_cmdline_response {
    uint64_t revision;
    BYTER_PTR(char *) cmdline;
};

struct byter_executable_cmdline_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_executable_cmdline_response *) response;
};

/* Firmware type */

#define BYTER_FIRMWARE_TYPE_REQUEST_ID { BYTER_COMMON_MAGIC, 0x8c2f75d90bef28a8, 0x7045a4688eac00c3 }

#define BYTER_FIRMWARE_TYPE_X86BIOS 0
#define BYTER_FIRMWARE_TYPE_EFI32 1
#define BYTER_FIRMWARE_TYPE_EFI64 2
#define BYTER_FIRMWARE_TYPE_SBI 3

struct byter_firmware_type_response {
    uint64_t revision;
    uint64_t firmware_type;
};

struct byter_firmware_type_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_firmware_type_response *) response;
};

/* Stack size */

#define BYTER_STACK_SIZE_REQUEST_ID { BYTER_COMMON_MAGIC, 0x224ef0460a8e8926, 0xe1cb0fc25f46ea3d }

struct byter_stack_size_response {
    uint64_t revision;
};

struct byter_stack_size_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_stack_size_response *) response;
    uint64_t stack_size;
};

/* HHDM */

#define BYTER_HHDM_REQUEST_ID { BYTER_COMMON_MAGIC, 0x48dcf1cb8ad2b852, 0x63984e959a98244b }

struct byter_hhdm_response {
    uint64_t revision;
    uint64_t offset;
};

struct byter_hhdm_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_hhdm_response *) response;
};

/* Framebuffer */

#define BYTER_FRAMEBUFFER_REQUEST_ID { BYTER_COMMON_MAGIC, 0x9d5827dcd881dd75, 0xa3148604f6fab11b }

#define BYTER_FRAMEBUFFER_RGB 1

struct byter_video_mode {
    uint64_t pitch;
    uint64_t width;
    uint64_t height;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
};

struct byter_framebuffer {
    BYTER_PTR(void *) address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
    uint8_t unused[7];
    uint64_t edid_size;
    BYTER_PTR(void *) edid;
    /* Response revision 1 */
    uint64_t mode_count;
    BYTER_PTR(struct byter_video_mode **) modes;
};

struct byter_framebuffer_response {
    uint64_t revision;
    uint64_t framebuffer_count;
    BYTER_PTR(struct byter_framebuffer **) framebuffers;
};

struct byter_framebuffer_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_framebuffer_response *) response;
};

/* Flanterm FB init params */

#define BYTER_FLANTERM_FB_INIT_PARAMS_REQUEST_ID { BYTER_COMMON_MAGIC, 0x3259399fe7c5f126, 0xe01c1c8c5db9d1a9 }

#define BYTER_FLANTERM_FB_ROTATE_0 0
#define BYTER_FLANTERM_FB_ROTATE_90 1
#define BYTER_FLANTERM_FB_ROTATE_180 2
#define BYTER_FLANTERM_FB_ROTATE_270 3

struct byter_flanterm_fb_init_params {
    BYTER_PTR(uint32_t *) canvas;
    uint64_t canvas_size;
    uint32_t ansi_colours[8];
    uint32_t ansi_bright_colours[8];
    uint32_t default_bg;
    uint32_t default_fg;
    uint32_t default_bg_bright;
    uint32_t default_fg_bright;
    BYTER_PTR(void *) font;
    uint64_t font_width;
    uint64_t font_height;
    uint64_t font_spacing;
    uint64_t font_scale_x;
    uint64_t font_scale_y;
    uint64_t margin;
    uint64_t rotation;
};

struct byter_flanterm_fb_init_params_response {
    uint64_t revision;
    uint64_t entry_count;
    BYTER_PTR(struct byter_flanterm_fb_init_params **) entries;
};

struct byter_flanterm_fb_init_params_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_flanterm_fb_init_params_response *) response;
};

/* Paging mode */

#define BYTER_PAGING_MODE_REQUEST_ID { BYTER_COMMON_MAGIC, 0x95c1a0edab0944cb, 0xa4e5cb3842f7488a }

#define BYTER_PAGING_MODE_X86_64_4LVL 0
#define BYTER_PAGING_MODE_X86_64_5LVL 1
#define BYTER_PAGING_MODE_X86_64_MIN BYTER_PAGING_MODE_X86_64_4LVL
#define BYTER_PAGING_MODE_X86_64_DEFAULT BYTER_PAGING_MODE_X86_64_4LVL

#define BYTER_PAGING_MODE_AARCH64_4LVL 0
#define BYTER_PAGING_MODE_AARCH64_5LVL 1
#define BYTER_PAGING_MODE_AARCH64_MIN BYTER_PAGING_MODE_AARCH64_4LVL
#define BYTER_PAGING_MODE_AARCH64_DEFAULT BYTER_PAGING_MODE_AARCH64_4LVL

#define BYTER_PAGING_MODE_RISCV_SV39 0
#define BYTER_PAGING_MODE_RISCV_SV48 1
#define BYTER_PAGING_MODE_RISCV_SV57 2
#define BYTER_PAGING_MODE_RISCV_MIN BYTER_PAGING_MODE_RISCV_SV39
#define BYTER_PAGING_MODE_RISCV_DEFAULT BYTER_PAGING_MODE_RISCV_SV48

#define BYTER_PAGING_MODE_LOONGARCH_4LVL 0
#define BYTER_PAGING_MODE_LOONGARCH_MIN BYTER_PAGING_MODE_LOONGARCH_4LVL
#define BYTER_PAGING_MODE_LOONGARCH_DEFAULT BYTER_PAGING_MODE_LOONGARCH_4LVL

struct byter_paging_mode_response {
    uint64_t revision;
    uint64_t mode;
};

struct byter_paging_mode_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_paging_mode_response *) response;
    uint64_t mode;
    uint64_t max_mode;
    uint64_t min_mode;
};

/* MP */

#define BYTER_MP_REQUEST_ID { BYTER_COMMON_MAGIC, 0x95a67b819a1b857e, 0xa0b61b723b6a73e0 }

struct byter_mp_info;

typedef void (*byter_goto_address)(struct byter_mp_info *);

#if defined (__x86_64__) || defined (__i386__) || defined(_M_X64) || defined(_M_AMD64) || defined(_M_IX86)

#define BYTER_MP_RESPONSE_X86_64_X2APIC (1 << 0)

struct byter_mp_info {
    uint32_t processor_id;
    uint32_t lapic_id;
    uint64_t reserved;
    BYTER_PTR(byter_goto_address) goto_address;
    uint64_t extra_argument;
};

struct byter_mp_response {
    uint64_t revision;
    uint32_t flags;
    uint32_t bsp_lapic_id;
    uint64_t cpu_count;
    BYTER_PTR(struct byter_mp_info **) cpus;
};

#elif defined (__aarch64__) || defined(_M_ARM64)

struct byter_mp_info {
    uint32_t processor_id;
    uint32_t reserved1;
    uint64_t mpidr;
    uint64_t reserved;
    BYTER_PTR(byter_goto_address) goto_address;
    uint64_t extra_argument;
};

struct byter_mp_response {
    uint64_t revision;
    uint64_t flags;
    uint64_t bsp_mpidr;
    uint64_t cpu_count;
    BYTER_PTR(struct byter_mp_info **) cpus;
};

#elif defined (__riscv) && (__riscv_xlen == 64)

struct byter_mp_info {
    uint64_t processor_id;
    uint64_t hartid;
    uint64_t reserved;
    BYTER_PTR(byter_goto_address) goto_address;
    uint64_t extra_argument;
};

struct byter_mp_response {
    uint64_t revision;
    uint64_t flags;
    uint64_t bsp_hartid;
    uint64_t cpu_count;
    BYTER_PTR(struct byter_mp_info **) cpus;
};

#elif defined (__loongarch__) && (__loongarch_grlen == 64)

struct byter_mp_info {
    uint64_t processor_id;
    uint64_t phys_id;
    uint64_t reserved;
    BYTER_PTR(byter_goto_address) goto_address;
    uint64_t extra_argument;
};

struct byter_mp_response {
    uint64_t revision;
    uint64_t flags;
    uint64_t bsp_phys_id;
    uint64_t cpu_count;
    BYTER_PTR(struct byter_mp_info **) cpus;
};

#else
#error Unknown architecture
#endif

#define BYTER_MP_REQUEST_X86_64_X2APIC (1 << 0)

struct byter_mp_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_mp_response *) response;
    uint64_t flags;
};

/* Memory map */

#define BYTER_MEMMAP_REQUEST_ID { BYTER_COMMON_MAGIC, 0x67cf3d9d378a806f, 0xe304acdfc50c3c62 }

#define BYTER_MEMMAP_USABLE                 0
#define BYTER_MEMMAP_RESERVED               1
#define BYTER_MEMMAP_ACPI_RECLAIMABLE       2
#define BYTER_MEMMAP_ACPI_NVS               3
#define BYTER_MEMMAP_BAD_MEMORY             4
#define BYTER_MEMMAP_BOOTLOADER_RECLAIMABLE 5
#define BYTER_MEMMAP_EXECUTABLE_AND_MODULES 6
#define BYTER_MEMMAP_FRAMEBUFFER            7
#define BYTER_MEMMAP_RESERVED_MAPPED        8

struct byter_memmap_entry {
    uint64_t base;
    uint64_t length;
    uint64_t type;
};

struct byter_memmap_response {
    uint64_t revision;
    uint64_t entry_count;
    BYTER_PTR(struct byter_memmap_entry **) entries;
};

struct byter_memmap_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_memmap_response *) response;
};

/* Entry point */

#define BYTER_ENTRY_POINT_REQUEST_ID { BYTER_COMMON_MAGIC, 0x13d86c035a1cd3e1, 0x2b0caa89d8f3026a }

typedef void (*byter_entry_point)(void);

struct byter_entry_point_response {
    uint64_t revision;
};

struct byter_entry_point_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_entry_point_response *) response;
    BYTER_PTR(byter_entry_point) entry;
};

/* Executable File */

#define BYTER_EXECUTABLE_FILE_REQUEST_ID { BYTER_COMMON_MAGIC, 0xad97e90e83f1ed67, 0x31eb5d1c5ff23b69 }

struct byter_executable_file_response {
    uint64_t revision;
    BYTER_PTR(struct byter_file *) executable_file;
};

struct byter_executable_file_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_executable_file_response *) response;
};

/* Module */

#define BYTER_MODULE_REQUEST_ID { BYTER_COMMON_MAGIC, 0x3e7e279702be32af, 0xca1c4f3bd1280cee }

#define BYTER_INTERNAL_MODULE_REQUIRED (1 << 0)
#define BYTER_INTERNAL_MODULE_COMPRESSED (1 << 1)

struct byter_internal_module {
    BYTER_PTR(const char *) path;
    BYTER_PTR(const char *) string;
    uint64_t flags;
};

struct byter_module_response {
    uint64_t revision;
    uint64_t module_count;
    BYTER_PTR(struct byter_file **) modules;
};

struct byter_module_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_module_response *) response;

    /* Request revision 1 */
    uint64_t internal_module_count;
    BYTER_PTR(struct byter_internal_module **) internal_modules;
};

/* RSDP */

#define BYTER_RSDP_REQUEST_ID { BYTER_COMMON_MAGIC, 0xc5e77b6b397e7b43, 0x27637845accdcf3c }

struct byter_rsdp_response {
    uint64_t revision;
    BYTER_PTR(void *) address;
};

struct byter_rsdp_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_rsdp_response *) response;
};

/* SMBIOS */

#define BYTER_SMBIOS_REQUEST_ID { BYTER_COMMON_MAGIC, 0x9e9046f11e095391, 0xaa4a520fefbde5ee }

struct byter_smbios_response {
    uint64_t revision;
    BYTER_PTR(void *) entry_32;
    BYTER_PTR(void *) entry_64;
};

struct byter_smbios_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_smbios_response *) response;
};

/* EFI system table */

#define BYTER_EFI_SYSTEM_TABLE_REQUEST_ID { BYTER_COMMON_MAGIC, 0x5ceba5163eaaf6d6, 0x0a6981610cf65fcc }

struct byter_efi_system_table_response {
    uint64_t revision;
    BYTER_PTR(void *) address;
};

struct byter_efi_system_table_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_efi_system_table_response *) response;
};

/* TPM event log */

#define BYTER_TPM_EVENT_LOG_REQUEST_ID { BYTER_COMMON_MAGIC, 0x98e094fc7e76e979, 0xee8d8775c54e1d1f }

#define BYTER_TPM_EVENT_LOG_FORMAT_TCG_1_2 1
#define BYTER_TPM_EVENT_LOG_FORMAT_TCG_2   2

struct byter_tpm_event_log_response {
    uint64_t revision;
    uint64_t format;
    uint64_t size;
    BYTER_PTR(void *) address;
};

struct byter_tpm_event_log_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_tpm_event_log_response *) response;
};

/* EFI memory map */

#define BYTER_EFI_MEMMAP_REQUEST_ID { BYTER_COMMON_MAGIC, 0x7df62a431d6872d5, 0xa4fcdfb3e57306c8 }

struct byter_efi_memmap_response {
    uint64_t revision;
    BYTER_PTR(void *) memmap;
    uint64_t memmap_size;
    uint64_t desc_size;
    uint64_t desc_version;
};

struct byter_efi_memmap_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_efi_memmap_response *) response;
};

/* Date at boot */

#define BYTER_DATE_AT_BOOT_REQUEST_ID { BYTER_COMMON_MAGIC, 0x502746e184c088aa, 0xfbc5ec83e6327893 }

struct byter_date_at_boot_response {
    uint64_t revision;
    int64_t timestamp;
};

struct byter_date_at_boot_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_date_at_boot_response *) response;
};

/* Executable address */

#define BYTER_EXECUTABLE_ADDRESS_REQUEST_ID { BYTER_COMMON_MAGIC, 0x71ba76863cc55f63, 0xb2644a48c516a487 }

struct byter_executable_address_response {
    uint64_t revision;
    uint64_t physical_base;
    uint64_t virtual_base;
};

struct byter_executable_address_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_executable_address_response *) response;
};

/* Device Tree Blob */

#define BYTER_DTB_REQUEST_ID { BYTER_COMMON_MAGIC, 0xb40ddb48fb54bac7, 0x545081493f81ffb7 }

struct byter_dtb_response {
    uint64_t revision;
    BYTER_PTR(void *) dtb_ptr;
};

struct byter_dtb_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_dtb_response *) response;
};

/* RISC-V Boot Hart ID */

#define BYTER_RISCV_BSP_HARTID_REQUEST_ID { BYTER_COMMON_MAGIC, 0x1369359f025525f9, 0x2ff2a56178391bb6 }

struct byter_riscv_bsp_hartid_response {
    uint64_t revision;
    uint64_t bsp_hartid;
};

struct byter_riscv_bsp_hartid_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_riscv_bsp_hartid_response *) response;
};

/* Bootloader Performance */

#define BYTER_BOOTLOADER_PERFORMANCE_REQUEST_ID { BYTER_COMMON_MAGIC, 0x6b50ad9bf36d13ad, 0xdc4c7e88fc759e17 }

struct byter_bootloader_performance_response {
    uint64_t revision;
    uint64_t reset_usec;
    uint64_t init_usec;
    uint64_t exec_usec;
};

struct byter_bootloader_performance_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_bootloader_performance_response *) response;
};

#define BYTER_X86_64_KEEP_IOMMU_REQUEST_ID { BYTER_COMMON_MAGIC, 0x8ebaabe51f490179, 0x2aa86a59ffb4ab0f }

struct byter_x86_64_keep_iommu_response {
    uint64_t revision;
};

struct byter_x86_64_keep_iommu_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_x86_64_keep_iommu_response *) response;
};

/* TSC (Timestamp Counter) Frequency */

#define BYTER_TSC_FREQUENCY_REQUEST_ID { BYTER_COMMON_MAGIC, 0x10f2ee1d87d195e4, 0xf747a2b78f6ddb31 }

struct byter_tsc_frequency_response {
    uint64_t revision;
    uint64_t frequency;
};

struct byter_tsc_frequency_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_tsc_frequency_response *) response;
};

/* Entropy */

#define BYTER_ENTROPY_REQUEST_ID { BYTER_COMMON_MAGIC, 0x65ea80255d5682c5, 0x9117240723f493eb }

struct byter_entropy_response {
    uint64_t revision;
    uint64_t value_count;
    BYTER_PTR(uint64_t *) values;
};

struct byter_entropy_request {
    uint64_t id[4];
    uint64_t revision;
    BYTER_PTR(struct byter_entropy_response *) response;
    uint64_t value_count;
};

#ifdef __cplusplus
}
#endif

#endif
