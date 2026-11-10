import io
import unittest
import wave
from unittest.mock import patch
from device_sampler_test import source_wav
from device_test import Device


class SamplerTests(unittest.TestCase):
    def test_fixture_is_stereo_at_non_native_rate(self):
        with wave.open(io.BytesIO(source_wav()), 'rb') as wav:
            self.assertEqual((wav.getnchannels(), wav.getframerate(), wav.getnframes()), (2, 16000, 32000))

    def test_dump_preserves_names_with_spaces_and_step_data(self):
        d = object.__new__(Device)
        with patch.object(d, 'send'), patch.object(d, 'wait_for', return_value='SAMPLER ready=1 recording=0'), patch.object(
            d, 'read_line', side_effect=['SOURCE live take.wav', 'MESSAGE Saved 1 slice(s)',
                'SLICE slot=0 start=123 end=456 source=live take.wav',
                'PAD index=0 slot=0 steps=x...............', 'SAMPLER-END']
        ):
            fields, clips, pads = d.sampler()
        self.assertEqual(fields['source'], 'live take.wav')
        self.assertEqual(clips[0]['source'], 'live take.wav')
        self.assertEqual(clips[0]['start'], 123)
        self.assertEqual(pads[0]['steps'], 'x...............')
