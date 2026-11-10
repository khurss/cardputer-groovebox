import unittest
from unittest.mock import patch
from device_test import Device


class ArrangementProtocolTests(unittest.TestCase):
    def test_arrangement_dump_keeps_empty_slots_and_strip_state(self):
        d = object.__new__(Device)
        with patch.object(d, 'send'), patch.object(d, 'wait_for', return_value='ARR song_length=1 master=5'), patch.object(
            d, 'read_line', side_effect=['SCENE index=0 t0=2 t1=-1', 'SONG index=0 scene=0 repeats=3',
                                        'MIX track=0 volume=75 mute=1 solo=0', 'ARR-END']
        ):
            fields, scenes, song, mixer = d.arrangement()
        self.assertEqual(fields['master'], 5)
        self.assertEqual(scenes[0]['t1'], -1)
        self.assertEqual(song[0]['repeats'], 3)
        self.assertEqual(mixer[0]['volume'], 75)

    def test_low_volume_audio_check_uses_output_and_enforces_ceiling(self):
        d = object.__new__(Device)
        with patch.object(d, 'send'), patch.object(d, 'wait_for', return_value='LEVEL peak=200 power=4000 output_peak=1000 output_power=500'):
            self.assertEqual(d.level(500), 500)
        with patch.object(d, 'send'), patch.object(d, 'wait_for', return_value='LEVEL peak=20 power=40 output_peak=2000 output_power=1000'):
            with self.assertRaisesRegex(RuntimeError, '5% ceiling'):
                d.level(500)

class ArpFixtureTests(unittest.TestCase):
    def test_heavy_arp_fixture_is_version_three_with_valid_checksum(self):
        import struct, zlib
        from device_arrangement_test import arpeggiated_stress_project
        image = arpeggiated_stress_project()
        version, length, crc = struct.unpack_from('<HII', image, 4)
        self.assertEqual(version, 3)
        self.assertEqual(length, len(image)-14)
        self.assertEqual(crc, zlib.crc32(image[14:]))
        self.assertEqual(image[-32], 5)
        self.assertEqual(image[-16:], bytes((1, 3, 3, 100))*4)


if __name__ == '__main__':
    unittest.main()
