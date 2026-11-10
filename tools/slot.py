#!/usr/bin/env python3
"""Installs the hardware probe into a spare OTA slot beside Launcher and GLIDE."""
import hashlib
import os
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

TABLE_OFFSET = 0x8000
OTADATA_OFFSET = 0xE000
PROBE_OFFSET = 0x260000
PROBE_SIZE = 0x100000
SAMPLES_OFFSET = 0x360000
SAMPLES_SIZE = 0x400000
PORT = "/dev/ttyACM0"
ROOT = Path(__file__).resolve().parent.parent
FORMAT = "<HBBII16sI"


def parse(data):
    entries = []
    for pos in range(0, len(data), 32):
        chunk = data[pos : pos + 32]
        if chunk[:2] != b"\xaa\x50":
            break
        entries.append(chunk)
    return entries


def check_table(data):
    entries = parse(data)
    end = len(entries) * 32
    body = b"".join(entries)
    expected = b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(body).digest()
    if data[end : end + 32] != expected:
        raise ValueError("partition table on the device is blank, corrupt or has a wrong MD5")
    wanted = (("app0", 0x10000), ("glide", 0x170000), ("otadata", OTADATA_OFFSET))
    found = {(label(e), struct.unpack(FORMAT, e)[3]) for e in entries}
    for name, offset in wanted:
        if (name, offset) not in found:
            raise ValueError(f"partition table has no {name} at {offset:#x}")


def build(entries):
    body = b"".join(entries)
    return (body + b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(body).digest()).ljust(0x1000, b"\xff")


def label(entry):
    return struct.unpack(FORMAT, entry)[5].rstrip(b"\0").decode()


def remove_probe(entries):
    return [e for e in entries if label(e) not in ("probe", "samples")]


def add_probe(entries):
    kept = remove_probe(entries)
    subtypes = []
    for e in kept:
        _, type_, subtype, offset, size, _, _ = struct.unpack(FORMAT, e)
        if offset < PROBE_OFFSET + PROBE_SIZE and PROBE_OFFSET < offset + size:
            raise ValueError(f"probe slot overlaps partition {label(e)}")
        if type_ == 0 and 0x10 <= subtype <= 0x1F:
            subtypes.append(subtype)
    next_subtype = max(subtypes, default=0x0F) + 1
    return kept + [struct.pack(FORMAT, 0x50AA, 0, next_subtype, PROBE_OFFSET, PROBE_SIZE, b"probe", 0)]


def add_samples(entries):
    kept = [e for e in entries if label(e) != "samples"]
    for e in kept:
        _, _, _, offset, size, _, _ = struct.unpack(FORMAT, e)
        if offset < SAMPLES_OFFSET + SAMPLES_SIZE and SAMPLES_OFFSET < offset + size:
            raise ValueError(f"samples partition overlaps partition {label(e)}")
    return kept + [struct.pack(FORMAT, 0x50AA, 1, 0x82, SAMPLES_OFFSET, SAMPLES_SIZE, b"samples", 0)]


def ota_index(entries, name):
    for e in entries:
        _, type_, subtype, _, _, _, _ = struct.unpack(FORMAT, e)
        if label(e) == name and type_ == 0 and 0x10 <= subtype <= 0x1F:
            return subtype - 0x10
    raise ValueError(f"no OTA app labelled {name}")


def otadata_sector(index):
    seq = struct.pack("<I", index + 1)
    crc = zlib.crc32(seq, 0xFFFFFFFF)
    return (seq + b"\xff" * 24 + struct.pack("<I", crc)).ljust(0x1000, b"\xff")


def otadata_image(index):
    return otadata_sector(index) + b"\xff" * 0x1000


def validate_args(argv):
    usage = "usage: slot.py install <bin> | install-with-samples <bin> | select <label> | remove-probe"
    if len(argv) < 2:
        sys.exit(usage)
    command = argv[1]
    if command in ("install", "install-with-samples"):
        if len(argv) < 3:
            sys.exit(usage)
        if not Path(argv[2]).is_file():
            sys.exit(f"probe image not found: {argv[2]}")
        with open(argv[2], "rb") as image:
            if image.read(1) != b"\xe9":
                sys.exit(f"not an ESP app image (first byte is not 0xE9): {argv[2]}")
    if command == "select" and len(argv) < 3:
        sys.exit(usage)
    if command not in ("install", "install-with-samples", "select", "remove-probe"):
        sys.exit(f"unknown command {command}")


def esptool(*args):
    gid = os.stat(PORT).st_gid
    image = os.environ.get("GB_IDF_IMAGE", "cbc2e2ec2afe")
    command = [
        "docker", "run", "--rm", f"--device={PORT}", f"--group-add={gid}",
        "-u", f"{os.getuid()}:{os.getgid()}", "-e", "HOME=/tmp",
        "-v", f"{ROOT}:/work", "-w", "/work", image,
        "esptool.py", "--chip", "esp32s3", "--port", PORT, *args,
    ]
    subprocess.run(command, check=True)


def write(work, offset, data):
    path = Path(work) / f"write-{offset:x}.bin"
    path.write_bytes(data)
    esptool("write_flash", hex(offset), "/work/" + str(path.relative_to(ROOT)))


def read_entries(work):
    path = Path(work) / "table.bin"
    esptool("read_flash", hex(TABLE_OFFSET), "0xC00", "/work/" + str(path.relative_to(ROOT)))
    data = path.read_bytes()
    check_table(data)
    return parse(data)


def main(argv):
    validate_args(argv)
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT / ".tmp") as work:
        try:
            entries = read_entries(work)
        except ValueError as error:
            sys.exit(f"refusing to write: {error}")
        if argv[1] == "install":
            app = Path(argv[2]).read_bytes()
            if len(app) > PROBE_SIZE:
                sys.exit("probe image does not fit its slot")
            table = add_probe(entries)
            write(work, PROBE_OFFSET, app)
            write(work, TABLE_OFFSET, build(table))
            write(work, OTADATA_OFFSET, otadata_image(ota_index(table, "probe")))
        elif argv[1] == "install-with-samples":
            app = Path(argv[2]).read_bytes()
            if len(app) > PROBE_SIZE:
                sys.exit("probe image does not fit its slot")
            table = add_samples(add_probe(entries))
            write(work, PROBE_OFFSET, app)
            write(work, SAMPLES_OFFSET, b"GBSM".ljust(0x1000, b"\xff"))
            write(work, TABLE_OFFSET, build(table))
            write(work, OTADATA_OFFSET, otadata_image(ota_index(table, "probe")))
        elif argv[1] == "select":
            write(work, OTADATA_OFFSET, otadata_image(ota_index(entries, argv[2])))
        elif argv[1] == "remove-probe":
            write(work, OTADATA_OFFSET, otadata_image(ota_index(entries, "glide")))
            write(work, TABLE_OFFSET, build(remove_probe(entries)))


if __name__ == "__main__":
    main(sys.argv)
