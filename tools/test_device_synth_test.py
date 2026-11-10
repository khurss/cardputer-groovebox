import struct
import unittest
import zlib
from unittest.mock import MagicMock, patch
import device_synth_test
import device_test


class SynthDeviceTests(unittest.TestCase):
    def test_stress_image_header_and_crc(self):
        for bpm in (40, 240):
            data = device_synth_test.stress_project(bpm)
            self.assertEqual(data[:4], b"GBPJ")
            version, length, crc = struct.unpack_from("<HII", data, 4)
            self.assertEqual(version, 2)
            self.assertEqual(length, len(data) - 14)
            self.assertEqual(crc, zlib.crc32(data[14:]))
            self.assertEqual(struct.unpack_from("<H", data, 14)[0], bpm)
            self.assertEqual(data[14 + 26888:14 + 26893], bytes(5))
            for track in range(4):
                self.assertEqual(data[14 + 2120 + track * 16 * 387 + 2], 64)

    def test_split_key_protocol(self):
        device = object.__new__(device_test.Device)
        with patch.object(device, "send") as send, patch.object(device, "wait_for"):
            device.down("z", "s")
            device.up("z", "s")
        self.assertEqual([c.args[0] for c in send.call_args_list], ["D s z", "U s z"])

    def test_instrument_dump(self):
        device = object.__new__(device_test.Device)
        with patch.object(device, "send"), patch.object(device, "wait_for", return_value="INSTR track=2 count=1"), patch.object(
            device, "read_line", side_effect=["NOTE slot=0 start=24 length=48 pitch=60 velocity=90", "INSTR-END"]
        ):
            fields, notes = device.instrument()
        self.assertEqual(fields, {"track": 2, "count": 1})
        self.assertEqual(notes[0]["length"], 48)

    def test_fixture_upload_is_bounded_and_complete(self):
        device = object.__new__(device_test.Device)
        with patch.object(device, "send") as send, patch.object(device, "wait_for"):
            device.upload_fixture(bytes(range(130)))
        lines = [c.args[0] for c in send.call_args_list]
        self.assertEqual(lines[0], "V begin 130")
        self.assertEqual(lines[-1], "V end")
        self.assertTrue(all(len(line) <= 135 for line in lines))
        self.assertEqual(bytes.fromhex("".join(line[7:] for line in lines[1:-1])), bytes(range(130)))


class IsolationTests(unittest.TestCase):
    def devices(self, changed=False):
        devices = [MagicMock(fd=i) for i in range(4)]
        devices[0].hashes.return_value = {"primary": {"size": 27111, "hash": 123}}
        devices[1].state.return_value = ({"test_mode": 1, "output_limit": 5, "master": 5}, {})
        devices[3].hashes.return_value = {"primary": {"size": 27111, "hash": 124 if changed else 123}}
        return devices

    def test_interruption_still_exits_test_mode_and_checks_user_files(self):
        devices = self.devices()
        with patch.object(device_test, "open_device", side_effect=devices), patch.object(device_test, "stop_for_test"), patch.object(
            device_test.os, "close"
        ), patch.object(device_test.time, "sleep"):
            with self.assertRaises(KeyboardInterrupt):
                with device_test.isolated_project():
                    raise KeyboardInterrupt()
        devices[2].send.assert_called_once_with("T end")
        devices[3].hashes.assert_called_once()

    def test_changed_user_files_fail_the_test(self):
        devices = self.devices(changed=True)
        with patch.object(device_test, "open_device", side_effect=devices), patch.object(device_test, "stop_for_test"), patch.object(
            device_test.os, "close"
        ), patch.object(device_test.time, "sleep"):
            with self.assertRaisesRegex(RuntimeError, "user project files changed"):
                with device_test.isolated_project():
                    pass


if __name__ == "__main__":
    unittest.main()
