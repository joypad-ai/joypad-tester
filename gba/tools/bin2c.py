#!/usr/bin/env python3
"""
bin2c.py — convert a binary GBA multiboot ROM into a C array source
file ready to compile into a host firmware that drives a joybus
multiboot uploader.

Usage:
    bin2c.py <input.gba> <output.c> [--name joypad_payload]

Default array name is `joypad_payload`. Override with --name for
projects that embed multiple payloads.
"""

import argparse
import os
import sys


def main():
    p = argparse.ArgumentParser()
    p.add_argument("input", help="binary input file (.gba multiboot ROM)")
    p.add_argument("output", help="C source output file")
    p.add_argument("--name", default="joypad_payload",
                   help="C array name (default: joypad_payload)")
    p.add_argument("--source-name", default=None,
                   help="logical name printed in the file's comment header")
    args = p.parse_args()

    with open(args.input, "rb") as f:
        data = f.read()

    if len(data) % 8 != 0:
        # Pad to multiple of 8 — multiboot upload sends 4-byte words and
        # the size formula expects 8-byte chunks.
        data = data + b"\x00" * (8 - len(data) % 8)

    if len(data) >= 0x40000:
        sys.exit(f"error: payload {len(data)} >= 0x40000 (256KB EWRAM cap)")
    if len(data) < 0x200:
        sys.exit(f"error: payload {len(data)} < 0x200 (multiboot minimum)")

    src = args.source_name or os.path.basename(args.input)
    lines = [
        f"// {os.path.basename(args.output)} — generated from {src} ({len(data)} bytes)",
        "// Do not edit by hand — regenerate with tools/bin2c.py",
        "#include <stdint.h>",
        "",
        f"const uint8_t {args.name}[] = {{",
    ]
    for i in range(0, len(data), 12):
        chunk = data[i:i + 12]
        lines.append("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    lines.append("};")
    lines.append(f"const uint32_t {args.name}_len = {len(data)};")

    with open(args.output, "w") as f:
        f.write("\n".join(lines) + "\n")

    print(f"wrote {args.output} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
