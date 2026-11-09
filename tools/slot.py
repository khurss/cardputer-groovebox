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


def build(entries):
    body = b"".join(entries)
    return (body + b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(body).digest()).ljust(0x1000, b"\xff")


def label(entry):
    return struct.unpack(FORMAT, entry)[5].rstrip(b"\0").decode()


def remove_probe(entries):
    return [e for e in entries if label(e) != "probe"]


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
    return parse(path.read_bytes())


def main(argv):
    if len(argv) < 2:
        sys.exit("usage: slot.py install <bin> | select <label> | remove-probe")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT / ".tmp") as work:
        entries = read_entries(work)
        if argv[1] == "install":
            app = Path(argv[2]).read_bytes()
            if len(app) > PROBE_SIZE:
                sys.exit("probe image does not fit its slot")
            table = add_probe(entries)
            write(work, PROBE_OFFSET, app)
            write(work, TABLE_OFFSET, build(table))
            write(work, OTADATA_OFFSET, otadata_sector(ota_index(table, "probe")))
        elif argv[1] == "select":
            write(work, OTADATA_OFFSET, otadata_sector(ota_index(entries, argv[2])))
        elif argv[1] == "remove-probe":
            write(work, OTADATA_OFFSET, otadata_sector(ota_index(entries, "glide")))
            write(work, TABLE_OFFSET, build(remove_probe(entries)))
        else:
            sys.exit(f"unknown command {argv[1]}")


if __name__ == "__main__":
    main(sys.argv)
