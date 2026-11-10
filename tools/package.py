#!/usr/bin/env python3
"""Builds the image Launcher's WebUI needs to create the probe's samples partition."""
import sys
from pathlib import Path

SAMPLES_OFFSET = 0x110000


def merge(bootloader, table, app):
    image = bytearray(b"\xff" * (SAMPLES_OFFSET + 0x1000))
    image[0 : len(bootloader)] = bootloader
    image[0x8000 : 0x8000 + len(table)] = table
    image[0x10000 : 0x10000 + len(app)] = app
    # Launcher ignores a data partition with no payload, so the marker also makes it real.
    image[SAMPLES_OFFSET : SAMPLES_OFFSET + 4] = b"GBSM"
    return bytes(image)


def main(build_dir):
    build = Path(build_dir)
    image = merge(
        (build / "bootloader" / "bootloader.bin").read_bytes(),
        (build / "partition_table" / "partition-table.bin").read_bytes(),
        (build / "probe.bin").read_bytes(),
    )
    (build / "probe-package.bin").write_bytes(image)


if __name__ == "__main__":
    main(sys.argv[1])
