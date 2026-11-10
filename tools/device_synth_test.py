#!/usr/bin/env python3
"""Exercises live synth/record/edit paths and measures combined audio load over USB."""
import os
from pathlib import Path
import struct
import sys
import time
import zlib

from device_test import (Report, isolated_project, is_louder, open_device,
                         reset_device, stop_for_test, MAX_LOAD_PERMILLE)

ROOT = Path(__file__).resolve().parent.parent
STRESS_SECONDS = int(os.environ.get("GB_STRESS_SECONDS", "60"))


def stress_project(bpm):
    """A v2 image with 256 coincident notes and all eight drum lanes, derived from the v1 fixture."""
    body = bytearray((ROOT / "tests/fixtures/stage2-v1.gbp").read_bytes()[14:])
    struct.pack_into("<H", body, 0, bpm)
    body[2] = 50
    for pattern in range(16):
        offset = 40 + pattern * 130
        body[offset:offset + 2] = bytes((16, 2))
        body[offset + 2:offset + 130] = bytes([127 if pattern == 0 else 0]) * 128
    for track in range(4):
        offset = 2120 + track * 16 * 387
        body[offset:offset + 3] = bytes((16, 2, 64))
        for slot in range(64):
            struct.pack_into("<HHBB", body, offset + 3 + slot * 6, 0, 384, 40 + slot, 127)
    body[26888:26893] = bytes(5)  # scene 0: pattern 0 in every track
    # waveform, attack, decay, sustain, release, cutoff, resonance, glide, level
    body += bytes((3, 0, 0, 127, 127, 127, 127, 127, 127)) * 4 + bytes([1]) * 4
    return b"GBPJ" + struct.pack("<HII", 2, len(body), zlib.crc32(body)) + body


def reopen(device):
    os.close(device.fd)
    reset_device()
    device = open_device()
    boot = device.read_boot()
    if any(line.startswith("ERROR") for line in boot):
        raise RuntimeError("boot errors: " + repr(boot))
    device.wait_ready()
    # READY means the screen/console is live; the sampler may still be preparing
    # its cache. Wait for that asynchronous boot work before fixture uploads.
    stop_for_test(device)
    return device, boot


def wait_recording(device, value):
    deadline = time.monotonic() + 5
    while True:
        fields, _ = device.state()
        if fields["recording"] == value and (value or not fields["busy"]):
            return fields
        if time.monotonic() >= deadline:
            raise TimeoutError("recorder state did not settle")
        time.sleep(0.02)


def _run_checks():
    report = Report()
    device = open_device()
    try:
        device.wait_ready()
        report.check("isolated project", device.state()[0].get("test_mode") == 1)
        quiet = device.level(400)
        device.key("2", "f")
        for name in ("z", "c", "b"):
            device.down(name)
        live = device.level(500)
        report.check("live chord rendered at 5 percent", is_louder(live, quiet), f"{live} vs {quiet}")
        report.check("three live voices", device.cpu()["active_voices"] == 3)
        device.key("=", "f")
        for name in ("z", "c", "b"):
            device.up(name)
        time.sleep(0.3)
        report.check("releases survive octave change", device.cpu()["active_voices"] == 0)
        device.key("-", "f")

        device.key("alt")
        report.check("record armed", device.state()[0]["armed"] == 1)
        device.key("space")
        wait_recording(device, 1)
        for name in ("z", "c", "b"):
            device.down(name)
        time.sleep(0.25)
        for name in ("z", "c", "b"):
            device.up(name)
        device.key("alt")
        wait_recording(device, 0)
        fields, notes = device.instrument()
        report.check("chord recorded", sorted(n["pitch"] for n in notes) == [60, 64, 67], str(notes))
        report.check("positive lengths and quantized starts", all(n["length"] > 0 and n["start"] % 24 == 0 for n in notes))
        playing = device.level(2400)
        report.check("recorded loop rendered at 5 percent", is_louder(playing, quiet), f"{playing} vs {quiet}")
        device.key("space")
        report.check("recorded project saved", "ok=1" in device.wait_for("SAVE"))
        time.sleep(0.5)
        report.check("stop releases voices", device.cpu()["active_voices"] == 0)
        report.check("quiet after stop", not is_louder(device.level(400), quiet))

        # Select the first recorded note, move pitch, length, velocity; undo restores it.
        device.key("enter")
        before = device.instrument()[1]
        device.key(";", "f")
        moved = device.instrument()[1]
        report.check("pitch edited", moved != before)
        device.key("z", "f")
        report.check("edit undone", device.instrument()[1] == before)
        device.key("q", "f")
        report.check("quantization disabled", device.instrument()[0]["quantize"] == 0)
        device.key("z", "f")
        report.check("quantization undo keeps notes", device.instrument()[0]["quantize"] == 1 and device.instrument()[1] == before)
        device.key("q", "f")
        device.key("s", "f")
        device.key(";", "f")  # wave saw -> square
        device.key("z", "f")
        report.check("synth parameter undone", device.instrument()[0]["wave"] == 2)
        device.key(";", "f")
        device.key("s", "f")
        device.wait_for("SAVE", 6)
        device, _ = reopen(device)
        device.key("2", "f")
        fields, restored = device.instrument()
        report.check("notes survive reboot", restored == before)
        report.check("synth and quantization survive reboot", fields["wave"] == 3 and fields["quantize"] == 0)

        # Compatibility is exercised on the actual SD load path, not just a host decode.
        device.upload_fixture((ROOT / "tests/fixtures/stage2-v1.gbp").read_bytes())
        device, boot = reopen(device)
        report.check("v1 loads without warning", any("LOAD result=0" in line for line in boot), str(boot))
        report.check("v1 tempo restored", device.state()[0]["bpm"] == 133)
        device.key("2", "f")
        device.key("enter")
        report.check("migrated project saves", "ok=1" in device.wait_for("SAVE", 6))

        for bpm in (40, 240):
            stop_for_test(device)
            device.upload_fixture(stress_project(bpm))
            device, _ = reopen(device)
            device.key("space")
            time.sleep(1)
            device.send("C reset")
            device.wait_for("CPU")
            initial = device.cpu()
            # Keep refreshing UI and reading counters throughout the measurement window.
            deadline = time.monotonic() + STRESS_SECONDS
            while time.monotonic() < deadline:
                device.state()
                time.sleep(min(1, max(0, deadline - time.monotonic())))
            measured = device.cpu()
            detail = f"{measured['load_permille'] / 10}% voices={measured['peak_voices']} heap={measured['heap_free']}"
            report.check(f"{bpm} BPM combined CPU", measured["load_permille"] <= MAX_LOAD_PERMILLE, detail)
            report.check(f"{bpm} BPM configured voice pool", measured["peak_voices"] == measured["voice_capacity"], str(measured["voice_capacity"]))
            for counter in ("tx_overflows", "rx_overflows", "failures", "event_overflows"):
                report.check(f"{bpm} BPM no new {counter}", measured[counter] == initial[counter], str(measured[counter]))
            # Monitoring while clips play deliberately exceeds the shared pool.
            device.key("2", "f")
            for key in ("z", "x", "c", "v", "b", "n", "m", ","):
                device.down(key)
            report.check("voice stealing stays bounded", device.cpu()["active_voices"] == measured["voice_capacity"])
            for key in ("z", "x", "c", "v", "b", "n", "m", ","):
                device.up(key)
            stop_for_test(device)
            time.sleep(4.2)
            report.check("stress stop silences all voices", device.cpu()["active_voices"] == 0)
            after_live = device.cpu()
            report.check(f"{bpm} BPM CPU including live and save", after_live["load_permille"] <= MAX_LOAD_PERMILLE,
                         f"{after_live['load_permille'] / 10}%")
            for counter in ("tx_overflows", "rx_overflows", "failures", "event_overflows"):
                report.check(f"{bpm} BPM live/save no new {counter}", after_live[counter] == initial[counter])
            print("INFO stress", bpm, after_live)
        print(f"{report.failures} failures")
        return 1 if report.failures else 0
    finally:
        os.close(device.fd)


def run():
    with isolated_project():
        return _run_checks()


if __name__ == "__main__":
    sys.exit(run())
