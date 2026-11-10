#!/usr/bin/env python3
"""Checks stage 4 through real keyboard paths, always inside the 5% test ceiling."""
import os
import time
from device_test import isolated_project, open_device, Report, is_louder, stop_for_test
from device_synth_test import reopen, wait_recording


def wait_state(device, **expected):
    deadline = time.monotonic() + 10
    while True:
        state, _ = device.state()
        if all(state.get(k) == v for k, v in expected.items()):
            return state
        if time.monotonic() >= deadline:
            raise TimeoutError(f"state did not reach {expected}: {state}")
        time.sleep(0.03)


def stop_saved(device):
    state, _ = device.state()
    if state["playing"]:
        device.key("space")
        device.wait_for("SAVE")
    wait_state(device, playing=0, busy=0)


def checks():
    r = Report()
    d = open_device()
    try:
        d.wait_ready()
        fields, _ = d.state()
        r.check("5 percent test ceiling", fields["master"] == 5 and fields["output_limit"] == 5)
        for page in range(1, 6):
            d.key(str(page), "f")
            d.key("-")
            r.check(f"global volume mutes on screen {page}", d.state()[0]["master"] == 0)
            d.key("=", "s")
            r.check(f"printed plus restores 5 percent on screen {page}", d.state()[0]["master"] == 5)
        d.key("-", "c")
        r.check("Ctrl gives one percent volume step", d.state()[0]["master"] == 4)
        d.key("+", "c")
        r.check("literal plus supports fine volume", d.state()[0]["master"] == 5)
        d.down("fn")
        d.key("2")
        r.check("held Fn menu navigates to synth", d.state()[0]["screen"] == 1)
        d.up("fn")
        d.down("z")
        d.level(200)
        d.down("fn")
        d.up("z")
        time.sleep(0.3)
        r.check("note release works with Fn menu open", d.cpu()["active_voices"] == 0)
        d.up("fn")
        d.key("1", "f")
        quiet = d.level(400)
        d.key("q")
        d.key("t")
        d.key("back", "f")
        r.check("clear drums from keyboard", set(d.state()[1][0]) == {"."})
        d.key("z", "f")
        r.check("undo restores drums", d.state()[1][0].startswith("x...x"))
        d.key("]")
        d.key("w")
        d.key("y")
        d.key("2", "f")
        d.key("=", "f")
        d.key("=", "f")
        d.key("alt")
        d.key("space")
        wait_recording(d, 1)
        d.down("z")
        d.down("c")
        r.check("view follows held high notes immediately", d.state()[0]["focus_pitch"] == 88)
        r.check("held notes shown before note completion", d.instrument()[0]["count"] == 0)
        time.sleep(0.2)
        d.up("z")
        d.up("c")
        d.key("alt")
        wait_recording(d, 0)
        r.check("high notes recorded", [n["pitch"] for n in d.instrument()[1]] == [84, 88])
        d.key("a", "f")
        r.check("arpeggiator enabled", d.instrument()[0]["arp"] == 1)
        d.down("z")
        d.down("c")
        r.check("arpeggiator rendered at 5 percent", is_louder(d.level(700), quiet))
        d.up("z")
        d.up("c")
        d.key("space")
        d.wait_for("SAVE")
        d.key("back", "f")
        r.check("clear synth from keyboard", d.instrument()[0]["count"] == 0)
        d.key("z", "f")
        r.check("undo restores synth", d.instrument()[0]["count"] == 2)
        d.key("a", "fs")
        d.key("o", "f")
        d.key("t", "f")

        d.key("5", "f")
        d.key("-")
        r.check("master can mute", d.state()[0]["master"] == 0)
        d.key("space")
        silent = d.level(1500)
        d.key("=")
        sounded = d.level(2400)
        r.check("master changes real audio", is_louder(sounded, silent), f"power {sounded} vs {silent}")
        d.key("/", "f")
        d.key("m")
        d.key("s")
        _, _, _, mixer = d.arrangement()
        r.check("drum mute and solo", mixer[0]["mute"] == 1 and mixer[0]["solo"] == 1)
        d.key("z", "f")
        d.key("m")
        d.key("-", "f")
        r.check("per track level", d.arrangement()[3][0]["volume"] == 95)
        stop_saved(d)

        d.key("3", "f")
        d.key("c", "f")
        d.key("]")
        d.key("v", "f")
        d.key("-", "f")
        d.key("2")
        d.key("back")
        _, scenes, _, _ = d.arrangement()
        r.check("independent scenes assembled from clips", scenes[0]["t0"] == 1 and scenes[0]["t1"] == 0
                and scenes[1]["t0"] == 0 and scenes[1]["t1"] == -1)
        d.key("[")
        d.key("4", "f")
        d.key("enter", "f")
        d.key(".", "f")
        d.key("3", "f")
        d.key("]")
        d.key("4", "f")
        d.key("enter", "f")
        d.key("/", "f")
        d.key("=", "f")
        song = d.arrangement()[2]
        r.check("song chain created", [(s["scene"], s["repeats"]) for s in song] == [(0, 1), (1, 2)])
        d.key(";", "f")
        d.key("space")
        wait_state(d, playing=1, song_mode=1, song_entry=0)
        wait_state(d, playing=1, song_entry=1, scene=1)
        wait_state(d, playing=0)
        d.key("space")
        wait_state(d, playing=1, song_entry=0)
        r.check("space restarts naturally ended song", True)
        d.key("3", "f")
        d.key("[")
        d.key("2", "f")
        time.sleep(0.6)
        d.key("alt")
        wait_recording(d, 1)
        d.down("q")
        wait_state(d, song_entry=1, busy=0)
        d.up("q")
        recorded = d.instrument()[1]
        r.check("song boundary finishes recorded block", len(recorded) == 3 and 0 < recorded[-1]["length"] < 384, str(recorded))
        stop_saved(d)

        d.key("3", "f")
        d.key("[")
        d.key("enter")
        wait_state(d, playing=1, scene=0)
        time.sleep(0.2)
        d.key("]")
        d.key("enter")
        r.check("scene launch queued", wait_state(d, scene=0, queued=2)["queued"] == 2)
        wait_state(d, scene=1, queued=0)
        d.key("2")
        d.key("=", "f")
        d.key("=", "f")
        d.key("[")
        d.key("2", "f")
        r.check("editor can prepare another scene", d.instrument()[0]["pattern"] == 0)
        d.key("alt")
        wait_recording(d, 1)
        r.check("record follows actual playing scene", d.instrument()[0]["pattern"] == 1)
        d.down("z")
        time.sleep(0.2)
        d.up("z")
        d.key("alt")
        wait_recording(d, 0)
        r.check("recorded second scene block", d.instrument()[0]["count"] == 1)
        stop_saved(d)
        d, boot = reopen(d)
        r.check("v4 loads after reset", any("LOAD result=0" in line for line in boot))
        fields, scenes, song, mixer = d.arrangement()
        r.check("scene and song survive reset", fields["song_length"] == 2 and song[1]["repeats"] == 2 and scenes[1]["t1"] == 1)
        r.check("mixer survives reset", mixer[0]["volume"] == 95 and mixer[0]["mute"] == 0)
        d.key("2", "f")
        arp = d.instrument()[0]
        r.check("arpeggiator survives reset", arp["arp"] == 1 and arp["arp_octaves"] == 2 and arp["arp_gate"] == 75)
        r.check("reopened pattern focuses its notes", d.state()[0]["focus_pitch"] == 84)
        r.check("test ceiling survives reset", d.state()[0]["master"] == 5 and d.state()[0]["output_limit"] == 5)
        print(f"{r.failures} failures")
        return 1 if r.failures else 0
    finally:
        os.close(d.fd)


def run():
    with isolated_project():
        return checks()


def arpeggiated_stress_project(bpm=240):
    import struct
    import zlib
    from device_synth_test import stress_project
    body = stress_project(bpm)[14:]
    body += bytes([5]) + bytes((100, 0, 0)) * 5 + bytes((1, 3, 3, 100)) * 4
    return b'GBPJ' + struct.pack('<HII', 3, len(body), zlib.crc32(body)) + body


def run_stress():
    from device_test import MAX_LOAD_PERMILLE
    with isolated_project():
        d = open_device()
        r = Report()
        try:
            d.wait_ready()
            d.upload_fixture(arpeggiated_stress_project())
            d, _ = reopen(d)
            d.key('space')
            time.sleep(1)
            d.send('C reset')
            d.wait_for('CPU')
            before = d.cpu()
            deadline = time.monotonic() + 60
            while time.monotonic() < deadline:
                d.state()
                time.sleep(min(1, max(0, deadline-time.monotonic())))
            d.level(500)  # Also asserts the actual PCM peak stays below the 5% ceiling.
            after = d.cpu()
            r.check('four arpeggiators plus all drums CPU', after['load_permille'] <= MAX_LOAD_PERMILLE,
                    f"{after['load_permille']/10}%")
            r.check('arp voices bounded', 0 < after['peak_voices'] <= after['voice_capacity'])
            for counter in ('tx_overflows', 'rx_overflows', 'failures', 'event_overflows'):
                r.check('arp stress no new '+counter, after[counter] == before[counter])
            stop_saved(d)
            time.sleep(4.2)
            r.check('arp stress releases all voices', d.cpu()['active_voices'] == 0)
            print('INFO arp stress', after)
            print(f'{r.failures} failures')
            return 1 if r.failures else 0
        finally:
            os.close(d.fd)


if __name__ == "__main__":
    import sys
    result = run()
    if not result and '--stress' in sys.argv:
        result = run_stress()
    raise SystemExit(result)
