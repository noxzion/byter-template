#!/usr/bin/env python3
import argparse
import pathlib
import struct
import subprocess

PAGE_SIZE = 4096
MAX_IMAGE_SIZE = 64 * 1024 * 1024
HANDOFF_OFFSET = 1024 * 1024
CATALOG_OFFSET_FIELD = 72
CATALOG_SIZE_FIELD = 80
FILE_SIZE_FIELD = 88
MAX_CONFIG_SIZE = 4096
MAX_CMDLINE_SIZE = 1024
MAX_LABEL_SIZE = 63
MAX_PATH_SIZE = 1024
MAX_STRING_SIZE = 1024
MAX_OPTIONS = 16
MAX_MODULES = 128
CATALOG_MAGIC = b"BYTERCAT"
CATALOG_VERSION = 2
CATALOG_HEADER = struct.Struct("<8sIIIII")
CATALOG_ENTRY = struct.Struct("<IIQQIIII")
CATALOG_MODULE = struct.Struct("<IIIIQQ")


def align(value, alignment=PAGE_SIZE):
    return (value + alignment - 1) & ~(alignment - 1)


def load_config(path):
    if path.stat().st_size > MAX_CONFIG_SIZE:
        raise SystemExit(f"config exceeds {MAX_CONFIG_SIZE} bytes: {path}")
    legacy = {}
    entries = []
    current = None
    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise SystemExit(f"invalid config line {line_number}: expected key=value")
        key, value = line.split("=", 1)
        key, value = key.strip().lower(), value.strip()
        if key == "entry":
            if current is not None:
                entries.append(current)
            if legacy:
                raise SystemExit("do not mix legacy kernel/cmdline with entry= blocks")
            current = {"name": value}
            continue
        values = current if current is not None else legacy
        if key == "module":
            values.setdefault("modules", []).append(value)
            continue
        if key not in {"kernel", "cmdline"}:
            raise SystemExit(f"unknown config key on line {line_number}: {key}")
        if key in values:
            raise SystemExit(f"duplicate {key}= on line {line_number}")
        values[key] = value

    if current is not None:
        entries.append(current)
    if entries and legacy:
        raise SystemExit("do not mix legacy kernel/cmdline with entry= blocks")
    if not entries:
        legacy["name"] = "Byter"
        entries = [legacy]
    if not entries[0].get("kernel"):
        raise SystemExit("each entry= block must set kernel=...")
    if len(entries) > MAX_OPTIONS:
        raise SystemExit(f"config supports at most {MAX_OPTIONS} boot entries")

    for entry in entries:
        try:
            label = entry.get("name", "").encode("ascii", errors="strict")
        except UnicodeEncodeError:
            raise SystemExit("entry names must use printable ASCII characters") from None
        if (not label or len(label) > MAX_LABEL_SIZE or
                any(byte < 0x20 or byte > 0x7e for byte in label)):
            raise SystemExit(f"entry name must be 1..{MAX_LABEL_SIZE} printable ASCII characters")
        if not entry.get("kernel"):
            raise SystemExit(f"entry {entry['name']!r} must set kernel=...")
        cmdline = entry.get("cmdline", "").encode("utf-8")
        if len(cmdline) > MAX_CMDLINE_SIZE or b"\0" in cmdline:
            raise SystemExit(f"cmdline must be at most {MAX_CMDLINE_SIZE} bytes and contain no NUL")
        entry["label_bytes"] = label
        entry["cmdline_bytes"] = cmdline
        modules = entry.get("modules", [])
        if len(modules) > MAX_MODULES:
            raise SystemExit(f"each entry supports at most {MAX_MODULES} modules")
        entry["modules"] = []
        for raw_module in modules:
            if not raw_module:
                raise SystemExit("module= path must not be empty")
            try:
                path_bytes = raw_module.encode("utf-8", errors="strict")
            except UnicodeEncodeError:
                raise SystemExit("module paths must be valid UTF-8") from None
            if len(path_bytes) > MAX_PATH_SIZE or b"\0" in path_bytes:
                raise SystemExit(f"module path must be at most {MAX_PATH_SIZE} UTF-8 bytes and contain no NUL")
            module_path = pathlib.Path(raw_module)
            if not module_path.is_absolute():
                module_path = path.parent / module_path
            entry["modules"].append({
                "path": raw_module,
                "path_bytes": path_bytes,
                "string_bytes": b"",
                "file_path": module_path.resolve(),
            })
        kernel = pathlib.Path(entry["kernel"])
        if not kernel.is_absolute():
            kernel = path.parent / kernel
        entry["kernel_path"] = kernel.resolve()
    if len({entry["label_bytes"] for entry in entries}) != len(entries):
        raise SystemExit("boot entry names must be unique")
    return entries


def main():
    parser = argparse.ArgumentParser(description="Pack Byter ARM64 shim + ELF as an Android boot image")
    parser.add_argument("--shim", required=True, type=pathlib.Path)
    parser.add_argument("--config", required=True, type=pathlib.Path)
    parser.add_argument("--elf", type=pathlib.Path, help="override kernel= from the config")
    parser.add_argument("--kernel-out", required=True, type=pathlib.Path)
    parser.add_argument("--boot-out", required=True, type=pathlib.Path)
    parser.add_argument("--mkbootimg", default="mkbootimg")
    args = parser.parse_args()

    entries = load_config(args.config)
    if args.elf:
        if len(entries) != 1:
            raise SystemExit("--elf can override kernel= only with a single boot entry")
        entries[0]["kernel_path"] = args.elf.resolve()
    shim = bytearray(args.shim.read_bytes())
    if len(shim) < FILE_SIZE_FIELD + 8 or shim[56:60] != b"ARM\x64":
        raise SystemExit("shim is missing the ARM64 Image header or pack metadata")

    total_modules = sum(len(entry["modules"]) for entry in entries)
    if total_modules > MAX_MODULES:
        raise SystemExit(f"catalog supports at most {MAX_MODULES} modules total")

    elf_blobs = []
    module_blobs = []
    for entry in entries:
        elf = entry["kernel_path"].read_bytes()
        if not elf.startswith(b"\x7fELF") or len(elf) > MAX_IMAGE_SIZE:
            raise SystemExit(f"{entry['kernel_path']} must be an ELF file that fits in the 64 MiB image window")
        elf_blobs.append(elf)
        for module in entry["modules"]:
            data = module["file_path"].read_bytes()
            if not data:
                raise SystemExit(f"{module['file_path']} must not be empty")
            if len(data) > MAX_IMAGE_SIZE:
                raise SystemExit(f"{module['file_path']} exceeds the 64 MiB module limit")
            module_blobs.append(data)

    entry_table_size = CATALOG_ENTRY.size * len(entries)
    module_table_size = CATALOG_MODULE.size * total_modules
    data_start = CATALOG_HEADER.size + entry_table_size + module_table_size
    strings = bytearray()
    module_records = []
    entry_module_ranges = []
    module_index = 0
    for entry in entries:
        module_first = module_index
        for module in entry["modules"]:
            path_offset = data_start + len(strings)
            strings += module["path_bytes"] + b"\0"
            string_offset = data_start + len(strings)
            strings += module["string_bytes"] + b"\0"
            module_records.append((path_offset, len(module["path_bytes"]),
                                  string_offset, len(module["string_bytes"]) + 1))
            module_index += 1
        entry_module_ranges.append((module_first, module_index - module_first))

    string_offsets = []
    for entry in entries:
        label_offset = data_start + len(strings)
        strings += entry["label_bytes"] + b"\0"
        cmdline_offset = data_start + len(strings)
        strings += entry["cmdline_bytes"] + b"\0"
        string_offsets.append((label_offset, len(entry["label_bytes"]),
                               cmdline_offset, len(entry["cmdline_bytes"]) + 1))

    catalog_size = CATALOG_HEADER.size + entry_table_size + module_table_size + len(strings)
    if catalog_size > 0xffffffff:
        raise SystemExit("boot catalog is too large")

    catalog_offset = align(len(shim))
    catalog_end = catalog_offset + catalog_size
    cursor = align(catalog_end)
    elf_offsets = []
    for elf in elf_blobs:
        elf_offsets.append(cursor)
        cursor = align(cursor + len(elf))
    module_offsets = []
    for module in module_blobs:
        module_offsets.append(cursor)
        cursor = align(cursor + len(module))

    catalog = bytearray(CATALOG_HEADER.pack(CATALOG_MAGIC, CATALOG_VERSION, len(entries),
                                             CATALOG_ENTRY.size, catalog_size, total_modules))
    for entry, offsets, elf_offset, elf, module_range in zip(
            entries, string_offsets, elf_offsets, elf_blobs, entry_module_ranges):
        label_offset, label_length, cmdline_offset, cmdline_size = offsets
        module_first, module_count = module_range
        catalog += CATALOG_ENTRY.pack(label_offset, label_length, elf_offset, len(elf),
                                      cmdline_offset, cmdline_size, module_first, module_count)
    for record, module_offset, module_blob in zip(module_records, module_offsets, module_blobs):
        path_offset, path_length, string_offset, string_length = record
        catalog += CATALOG_MODULE.pack(path_offset, path_length, string_offset, string_length,
                                       module_offset, len(module_blob))
    catalog += strings

    kernel = shim + bytes(catalog_offset - len(shim)) + catalog
    for elf_offset, elf in zip(elf_offsets, elf_blobs):
        kernel += bytes(elf_offset - len(kernel)) + elf
    for module_offset, module_blob in zip(module_offsets, module_blobs):
        kernel += bytes(module_offset - len(kernel)) + module_blob
    if len(kernel) > MAX_IMAGE_SIZE - HANDOFF_OFFSET:
        raise SystemExit("boot entries and modules overlap the reserved handoff area in the 64 MiB image window")

    struct.pack_into("<Q", kernel, 16, MAX_IMAGE_SIZE)
    struct.pack_into("<Q", kernel, CATALOG_OFFSET_FIELD, catalog_offset)
    struct.pack_into("<Q", kernel, CATALOG_SIZE_FIELD, catalog_size)
    struct.pack_into("<Q", kernel, FILE_SIZE_FIELD, len(kernel))
    args.kernel_out.parent.mkdir(parents=True, exist_ok=True)
    args.kernel_out.write_bytes(kernel)

    args.boot_out.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        args.mkbootimg,
        "--header_version", "4",
        "--pagesize", str(PAGE_SIZE),
        "--kernel", str(args.kernel_out),
        "--os_version", "0.0.0",
        "--os_patch_level", "2026-11",
        "--cmdline", "",
        "-o", str(args.boot_out),
    ], check=True)
    print(f"Wrote {args.boot_out} ({args.boot_out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
