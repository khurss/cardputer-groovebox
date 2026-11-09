import hashlib
import struct
import tempfile
import unittest

import slot


def entry(type_, subtype, offset, size, label):
    return struct.pack("<HBBII16sI", 0x50AA, type_, subtype, offset, size, label.encode(), 0)


DEVICE = [
    entry(1, 0x02, 0x9000, 0x5000, "nvs"),
    entry(1, 0x00, 0xE000, 0x2000, "otadata"),
    entry(0, 0x20, 0x10000, 0x150000, "app0"),
    entry(1, 0x03, 0x160000, 0x10000, "coredump"),
    entry(0, 0x10, 0x170000, 0xF0000, "glide"),
]


class TableTests(unittest.TestCase):
    def test_build_appends_md5_and_pads(self):
        data = slot.build(DEVICE)
        body = b"".join(DEVICE)
        self.assertEqual(data[:160], body)
        self.assertEqual(data[160:176], b"\xeb\xeb" + b"\xff" * 14)
        self.assertEqual(data[176:192], hashlib.md5(body).digest())
        self.assertEqual(len(data), 0x1000)
        self.assertEqual(slot.parse(data), DEVICE)

    def test_add_probe_uses_next_ota_subtype(self):
        entries = slot.add_probe(DEVICE)
        fields = struct.unpack("<HBBII16sI", entries[-1])
        self.assertEqual(fields[1:5], (0, 0x11, 0x260000, 0x100000))
        self.assertEqual(slot.label(entries[-1]), "probe")

    def test_add_probe_is_idempotent(self):
        self.assertEqual(slot.add_probe(slot.add_probe(DEVICE)), slot.add_probe(DEVICE))

    def test_add_probe_rejects_overlap(self):
        taken = DEVICE + [entry(1, 0x82, 0x2A0000, 0x1000, "other")]
        with self.assertRaises(ValueError):
            slot.add_probe(taken)

    def test_remove_probe_restores_table(self):
        self.assertEqual(slot.remove_probe(slot.add_probe(DEVICE)), DEVICE)

    def test_otadata_for_first_slot_has_known_crc(self):
        sector = slot.otadata_sector(0)
        self.assertEqual(sector[:4], struct.pack("<I", 1))
        self.assertEqual(sector[28:32], struct.pack("<I", 0x4743989A))
        self.assertEqual(len(sector), 0x1000)

    def test_ota_index_of_label(self):
        self.assertEqual(slot.ota_index(slot.add_probe(DEVICE), "glide"), 0)
        self.assertEqual(slot.ota_index(slot.add_probe(DEVICE), "probe"), 1)
        with self.assertRaises(ValueError):
            slot.ota_index(DEVICE, "missing")

    def test_otadata_image_clears_both_sectors(self):
        image = slot.otadata_image(1)
        self.assertEqual(len(image), 0x2000)
        self.assertEqual(image[:0x1000], slot.otadata_sector(1))
        self.assertEqual(image[0x1000:], b"\xff" * 0x1000)


class ArgumentTests(unittest.TestCase):
    def test_no_command_is_rejected(self):
        with self.assertRaises(SystemExit):
            slot.validate_args(["slot.py"])

    def test_unknown_command_is_rejected(self):
        with self.assertRaises(SystemExit):
            slot.validate_args(["slot.py", "flash"])

    def test_install_without_path_is_rejected(self):
        with self.assertRaises(SystemExit):
            slot.validate_args(["slot.py", "install"])

    def test_select_without_label_is_rejected(self):
        with self.assertRaises(SystemExit):
            slot.validate_args(["slot.py", "select"])

    def test_install_with_missing_file_is_rejected(self):
        with self.assertRaises(SystemExit):
            slot.validate_args(["slot.py", "install", "/nonexistent/path"])

    def test_valid_forms_are_accepted(self):
        with tempfile.NamedTemporaryFile() as image:
            slot.validate_args(["slot.py", "install", image.name])
        slot.validate_args(["slot.py", "select", "glide"])
        slot.validate_args(["slot.py", "remove-probe"])


if __name__ == "__main__":
    unittest.main()
