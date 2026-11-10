import unittest
from unittest.mock import Mock

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


class CleanupTests(unittest.TestCase):
    def test_live_microphone_and_transport_are_both_stopped(self):
        device = Mock()
        device.state.side_effect = [
            ({"playing": 1, "busy": 1, "sampler_recording": 1}, {}),
            ({"playing": 0, "busy": 0}, {}),
        ]
        device_test.stop_for_test(device)
        self.assertEqual([c.args for c in device.key.call_args_list], [("space",), ("6", "f"), ("alt",)])


if __name__ == "__main__":
    unittest.main()
