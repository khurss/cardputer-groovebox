import unittest

import device_test


class ParseTests(unittest.TestCase):
    def test_parse_fields(self):
        fields = device_test.parse_fields("STATE bpm=120 swing=50 playing=1")
        self.assertEqual(fields, {"bpm": 120, "swing": 50, "playing": 1})

    def test_ignores_words_without_a_value(self):
        self.assertEqual(device_test.parse_fields("LEVEL peak=12 extra"), {"peak": 12})

    def test_louder_passes_at_threshold(self):
        self.assertTrue(device_test.is_louder(4000, 1000))
        self.assertFalse(device_test.is_louder(3999, 1000))

    def test_quiet_reference_of_zero_is_handled(self):
        self.assertTrue(device_test.is_louder(10, 0))

    def test_silence_is_not_louder_than_silence(self):
        self.assertFalse(device_test.is_louder(0, 0))


if __name__ == "__main__":
    unittest.main()
