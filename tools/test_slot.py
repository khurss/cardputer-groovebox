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

    def test_add_samples_appends_data_partition(self):
        last = slot.add_samples(slot.add_probe(DEVICE))[-1]
        _, type_, subtype, offset, size, _, _ = struct.unpack(slot.FORMAT, last)
        self.assertEqual((type_, subtype, offset, size), (1, 0x82, 0x360000, 0x400000))
        self.assertEqual(slot.label(last), "samples")

    def test_add_samples_is_idempotent(self):
        once = slot.add_samples(slot.add_probe(DEVICE))
        self.assertEqual(slot.add_samples(once), once)

    def test_add_samples_rejects_overlap(self):
        taken = DEVICE + [entry(1, 0x82, 0x400000, 0x1000, "other")]
        with self.assertRaises(ValueError):
            slot.add_samples(taken)

    def test_remove_probe_also_removes_samples(self):
        self.assertEqual(slot.remove_probe(slot.add_samples(slot.add_probe(DEVICE))), DEVICE)

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


class CheckTableTests(unittest.TestCase):
    def good(self):
        return slot.build(DEVICE)[:0xC00]

    def test_valid_table_passes(self):
        slot.check_table(self.good())

    def test_valid_table_with_probe_and_samples_passes(self):
        slot.check_table(slot.build(slot.add_samples(slot.add_probe(DEVICE)))[:0xC00])

    def test_blank_table_is_rejected(self):
        with self.assertRaises(ValueError):
            slot.check_table(b"\xff" * 0xC00)

    def test_corrupted_third_magic_is_rejected(self):
        data = bytearray(self.good())
        data[64:66] = b"\x00\x00"
        with self.assertRaises(ValueError):
            slot.check_table(bytes(data))

    def test_wrong_md5_is_rejected(self):
        data = bytearray(self.good())
        data[len(DEVICE) * 32 + 16] ^= 0xFF
        with self.assertRaises(ValueError):
            slot.check_table(bytes(data))

    def test_missing_glide_is_rejected(self):
        without = [e for e in DEVICE if slot.label(e) != "glide"]
        with self.assertRaises(ValueError):
            slot.check_table(slot.build(without)[:0xC00])


class ArgumentTests(unittest.TestCase):
    def test_install_with_non_esp_image_is_rejected(self):
        for command in ("install", "install-with-samples"):
            with tempfile.NamedTemporaryFile() as image:
                image.write(b"\x00junk")
                image.flush()
                with self.assertRaises(SystemExit):
                    slot.validate_args(["slot.py", command, image.name])

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

    def test_install_with_samples_without_path_is_rejected(self):
        with self.assertRaises(SystemExit):
            slot.validate_args(["slot.py", "install-with-samples"])

    def test_install_with_samples_with_missing_file_is_rejected(self):
        with self.assertRaises(SystemExit):
            slot.validate_args(["slot.py", "install-with-samples", "/nonexistent/path"])

    def test_valid_forms_are_accepted(self):
        with tempfile.NamedTemporaryFile() as image:
            image.write(b"\xe9rest")
            image.flush()
            slot.validate_args(["slot.py", "install", image.name])
            slot.validate_args(["slot.py", "install-with-samples", image.name])
        slot.validate_args(["slot.py", "select", "glide"])
        slot.validate_args(["slot.py", "remove-probe"])


if __name__ == "__main__":
    unittest.main()
