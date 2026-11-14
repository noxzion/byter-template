#!/usr/bin/env python3
import argparse
import pathlib
import struct
import subprocess

PAGE_SIZE = 4096
MAX_IMAGE_SIZE = 64 * 1024 * 1024
HANDOFF_OFFSET = 1024 * 1024
ELF_OFFSET_FIELD = 72
ELF_SIZE_FIELD = 80
FILE_SIZE_FIELD = 88
CMDLINE_OFFSET_FIELD = 96
CMDLINE_SIZE_FIELD = 104
MAX_CONFIG_SIZE = 4096
MAX_CMDLINE_SIZE = 1024


def load_config(path):
    if path.stat().st_size > MAX_CONFIG_SIZE:
        raise SystemExit(f"config exceeds {MAX_CONFIG_SIZE} bytes: {path}")
    values = {}
    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise SystemExit(f"invalid config line {line_number}: expected key=value")
        key, value = line.split("=", 1)
        key, value = key.strip().lower(), value.strip()
        if key not in {"kernel", "cmdline"}:
            raise SystemExit(f"unknown config key on line {line_number}: {key}")
        if key in values:
            raise SystemExit(f"duplicate config key on line {line_number}: {key}")
        values[key] = value
    if not values.get("kernel"):
        raise SystemExit("config must set kernel=...")
    cmdline = values.get("cmdline", "")
    encoded_cmdline = cmdline.encode("utf-8")
    if len(encoded_cmdline) > MAX_CMDLINE_SIZE or b"\0" in encoded_cmdline:
        raise SystemExit(f"cmdline must be at most {MAX_CMDLINE_SIZE} bytes and contain no NUL")
    kernel = pathlib.Path(values["kernel"])
    if not kernel.is_absolute():
        kernel = path.parent / kernel
    return kernel.resolve(), encoded_cmdline


def main():
    parser = argparse.ArgumentParser(description="Pack Byter ARM64 shim + ELF as an Android boot image")
    parser.add_argument("--shim", required=True, type=pathlib.Path)
    parser.add_argument("--config", required=True, type=pathlib.Path)
    parser.add_argument("--elf", type=pathlib.Path, help="override kernel= from the config")
    parser.add_argument("--kernel-out", required=True, type=pathlib.Path)
    parser.add_argument("--boot-out", required=True, type=pathlib.Path)
    parser.add_argument("--mkbootimg", default="mkbootimg")
    args = parser.parse_args()

    configured_elf, cmdline = load_config(args.config)
    elf_path = args.elf.resolve() if args.elf else configured_elf
    shim = bytearray(args.shim.read_bytes())
    elf = elf_path.read_bytes()
    if len(shim) < CMDLINE_SIZE_FIELD + 8 or shim[56:60] != b"ARM\x64":
        raise SystemExit("shim is missing the ARM64 Image header or pack metadata")
    if not elf.startswith(b"\x7fELF") or len(elf) > MAX_IMAGE_SIZE:
        raise SystemExit("input must be an ELF file that fits in the 64 MiB shim window")

    elf_offset = (len(shim) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)
    elf_end = elf_offset + len(elf)
    cmdline_offset = (elf_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)
    cmdline_blob = cmdline + b"\0"
    kernel = shim + bytes(elf_offset - len(shim)) + elf
    kernel += bytes(cmdline_offset - len(kernel)) + cmdline_blob
    if len(kernel) > MAX_IMAGE_SIZE - HANDOFF_OFFSET:
        raise SystemExit("shim and ELF overlap the reserved handoff area in the 64 MiB image window")

    struct.pack_into("<Q", kernel, 16, MAX_IMAGE_SIZE)
    struct.pack_into("<Q", kernel, ELF_OFFSET_FIELD, elf_offset)
    struct.pack_into("<Q", kernel, ELF_SIZE_FIELD, len(elf))
    struct.pack_into("<Q", kernel, FILE_SIZE_FIELD, len(kernel))
    struct.pack_into("<Q", kernel, CMDLINE_OFFSET_FIELD, cmdline_offset)
    struct.pack_into("<Q", kernel, CMDLINE_SIZE_FIELD, len(cmdline_blob))
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
