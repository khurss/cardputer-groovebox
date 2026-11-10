import unittest

import package


class PackageTests(unittest.TestCase):
    def test_places_every_part_at_its_flash_offset(self):
        data = package.merge(b"B" * 16, b"\xaa\x50\x01" + b"T" * 13, b"\xe9" + b"A" * 15)
        self.assertEqual(data[0:16], b"B" * 16)
        self.assertEqual(data[0x8000:0x8003], b"\xaa\x50\x01")
        self.assertEqual(data[0x10000], 0xE9)
        self.assertEqual(data[package.SAMPLES_OFFSET : package.SAMPLES_OFFSET + 4], b"GBSM")

    def test_ends_right_after_the_marker_sector(self):
        data = package.merge(b"B", b"T", b"A")
        self.assertEqual(len(data), package.SAMPLES_OFFSET + 0x1000)

    def test_gap_is_erased_flash(self):
        data = package.merge(b"B", b"T", b"A")
        self.assertEqual(data[1], 0xFF)


if __name__ == "__main__":
    unittest.main()
