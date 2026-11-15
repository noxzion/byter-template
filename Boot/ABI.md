# Byter request ABI status

This document describes the behavior implemented by the current AArch64 shim.
The structures and request IDs in [`include/byter.h`](include/byter.h) retain
the Limine wire values, but this is a partial Byter implementation and does not
claim full Limine Boot Protocol compatibility.

## Common rules

- The example requests base revision 0 with a base-revision tag. The shim
  rejects tags requesting another revision and writes the selected revision 0
  and success value back to the tag. Without a tag, base revision 0 is assumed.
- Request objects are found at 8-byte-aligned addresses inside loaded ELF
  `PT_LOAD` segments. Before reading or writing a request, the shim checks that
  its 48-byte common header fits in that segment; it does not access
  request-specific trailing fields. The shim searches between the last start
  marker and the first end marker when the bounds are valid. With base revision
  0, absent or malformed delimiters fall back to searching the loaded image.
- Only request revision 0 is supported. For requests with the common Byter
  magic, the shim clears `response` first; unknown IDs and unsupported request
  revisions therefore remain `NULL`.
- Response objects are revision 0. All non-NULL pointers returned in responses
  and request `response` fields use the HHDM offset. The shim establishes 4 KiB
  page tables before entering the kernel: the first 4 GiB, DTB-declared RAM,
  and framebuffer are mapped at the HHDM offset; identity mappings cover the
  first 4 GiB except page zero, RAM, shim, DTB, handoff data, and framebuffer.
  ELF segments receive higher-half mappings with permissions derived from
  `PT_LOAD` flags.

## Implemented requests

| Request | Request revision | Response revision | Fields provided | Source and limits |
| --- | ---: | ---: | --- | --- |
| `BYTER_BOOTLOADER_INFO_REQUEST_ID` | 0 | 0 | `name`, `version` | Static strings `Byter AArch64 shim` and `0.1`. |
| `BYTER_EXECUTABLE_CMDLINE_REQUEST_ID` | 0 | 0 | `cmdline` | NUL-terminated UTF-8 command line from the selected build config; limited to 1024 bytes. |
| `BYTER_DTB_REQUEST_ID` | 0 | 0 | `dtb_ptr` | Original DTB pointer received from ABL. The response does not provide a DTB size. |
| `BYTER_FRAMEBUFFER_REQUEST_ID` | 0 | 0 | One framebuffer pointer and its address, dimensions, pitch, bpp, RGB masks, and memory model | Parsed from a DTB `simple-framebuffer` node; otherwise the A059 fallback profile. If no framebuffer is available, `response` remains `NULL`. EDID and video modes are not provided. |
| `BYTER_HHDM_REQUEST_ID` | 0 | 0 | `offset` | Fixed AArch64 HHDM base `0xffff800000000000`; maps the first 4 GiB, DTB RAM, framebuffer, and known reserved ranges above 4 GiB. |
| `BYTER_MEMMAP_REQUEST_ID` | 0 | 0 | `entry_count`, pointer array of `{base, length, type}` entries | RAM ranges from DTB, split around `/reserved-memory`, FDT memory-reservation entries, shim, DTB, ELF `PT_LOAD` ranges, selected module payload pages, and framebuffer. Known reservations and device `reg` ranges translated through each parent bus `ranges` outside RAM are also returned as `RESERVED`. Untranslatable device ranges and ranges beyond the bounded input capacity are omitted. Limited to 512 sorted, non-overlapping output entries. |
| `BYTER_EXECUTABLE_ADDRESS_REQUEST_ID` | 0 | 0 | `physical_base`, `virtual_base` | Lowest loaded `PT_LOAD` physical and virtual bases. The loader accepts AArch64 `ET_EXEC` segments with a uniform load bias, page-compatible addresses, and non-overlapping mappings; the example uses a higher-half VMA. |
| `BYTER_MODULE_REQUEST_ID` | 0 | 0 | `module_count`, array of `struct byter_file *` | Files attached to the selected catalog entry. Module payloads are embedded in the Android kernel Image and stay available for the kernel lifetime; their memory-map ranges are `EXECUTABLE_AND_MODULES`. Paths are the config values, strings are currently empty, media type is generic, and module request revision 1/internal modules and compression are unsupported. |

The current handoff enters at EL1 with the MMU and caches enabled, using a
4 KiB translation granule and 48-bit virtual addressing. Base-revision-0
identity mappings cover `0x1000..4 GiB`, all DTB RAM ranges, and known reserved
ranges above 4 GiB; HHDM mappings cover `0..4 GiB`, those RAM/reserved ranges,
and framebuffer. Physical address
`0..0x1000` is not marked usable. The example enters at or above
`0xffffffff80000000`, installs its own exception vector table, and runs with
interrupts masked. The handoff provides a 192 KiB stack, zeroes general-purpose
registers, configures `MAIR_EL1`, `TCR_EL1`, `TTBR0_EL1`, `TTBR1_EL1`, and
`SCTLR_EL1`, and enters at EL1. When starting at EL2 it clears trap controls and
drops to EL1 first. The example installs a vector table and verifies one
current-EL SVC exception/return path; other exception classes are not yet
recovered.

The shim's memory map is still based on DTB RAM, reservations, and device ranges
that can be translated unambiguously through `ranges`, not a complete firmware
physical-address map. An absent or malformed bus mapping causes that device
range to be omitted. Known map regions are identity- and HHDM-mapped as required
by the selected base-revision-0 subset. This implementation does not claim full
Limine base-revision-0 compatibility; the tag currently negotiates the Byter
handoff contract described here.

The shim prints boot progress directly to the framebuffer using its built-in
8x16 bitmap font; it does not clear the full screen first. It reports major
loading stages and the specific stage of page-table setup failures. The last
printed line remains visible if execution stops. The font bitmap is CC0.

## Android boot image catalog

The packer embeds a bounded catalog after the shim. It stores up to 16 entries;
each entry has a printable ASCII label (1–63 bytes), an AArch64 ELF, a
NUL-terminated UTF-8 command line (0–1024 bytes), and up to 128 modules total
across the catalog. Repeat `module=PATH` within an entry to attach files; paths
are resolved relative to the config file and retained as the module path in
`struct byter_file`. Module strings are currently empty. The version 2 catalog
validates offsets, lengths, string terminators, module ownership, and
non-overlapping ELF/module payloads; version 1 catalogs remain readable.
Volume Up/Down move through entries, Power selects, Essential Key requests a
reboot into the A059 bootloader Fastboot mode, and timeout selects entry zero.
The centered menu is drawn over the existing splash framebuffer without clearing
its logo. The legacy `kernel=`/`cmdline=` config maps to one entry named
`Byter` and may also include `module=` lines.

## Declared but not implemented

The following IDs are declared in `byter.h`, but the shim does not implement
their responses. Requests using them receive `response == NULL`:

| Request | Request | Request |
| --- | --- | --- |
| `BYTER_FIRMWARE_TYPE_REQUEST_ID` | `BYTER_STACK_SIZE_REQUEST_ID` | — |
| `BYTER_FLANTERM_FB_INIT_PARAMS_REQUEST_ID` | `BYTER_PAGING_MODE_REQUEST_ID` | `BYTER_MP_REQUEST_ID` |
| `BYTER_ENTRY_POINT_REQUEST_ID` | `BYTER_EXECUTABLE_FILE_REQUEST_ID` | `BYTER_RSDP_REQUEST_ID` |
| `BYTER_SMBIOS_REQUEST_ID` | `BYTER_EFI_SYSTEM_TABLE_REQUEST_ID` |
| `BYTER_TPM_EVENT_LOG_REQUEST_ID` | `BYTER_EFI_MEMMAP_REQUEST_ID` | `BYTER_DATE_AT_BOOT_REQUEST_ID` |
| `BYTER_RISCV_BSP_HARTID_REQUEST_ID` | `BYTER_BOOTLOADER_PERFORMANCE_REQUEST_ID` | `BYTER_X86_64_KEEP_IOMMU_REQUEST_ID` |
| `BYTER_TSC_FREQUENCY_REQUEST_ID` | `BYTER_ENTROPY_REQUEST_ID` | — |

This list is intentionally based on the IDs currently declared in the public
header. Adding a response requires implementing its data source and handoff
semantics in the shim, updating this table, and verifying it with a kernel
requesting that feature.
