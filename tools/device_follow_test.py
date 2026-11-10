#!/usr/bin/env python3
"""Check arrangement follow/pinned rows with real transport at a 5% ceiling."""
import os
from pathlib import Path
import struct
import time
import zlib
from device_test import Report, isolated_project, open_device, stop_for_test
from device_synth_test import reopen
from device_arrangement_test import wait_state


def fixture():
    # Legacy v1 layout: 16 five-track scenes, then 64 (scene, repeats) rows.
    root = Path(__file__).resolve().parent.parent
    body = bytearray((root / 'tests/fixtures/stage2-v1.gbp').read_bytes()[14:])
    assert len(body) == 27097
    struct.pack_into('<H', body, 0, 240)
    for scene in range(16):
        body[26888 + scene * 5:26893 + scene * 5] = bytes((0, 255, 255, 255, 255))
    for row in range(64):
        body[26968 + row * 2:26970 + row * 2] = bytes(((row * 7) % 16, 8 if row == 63 else 1))
    body[27096] = 64
    return b'GBPJ' + struct.pack('<HII', 1, len(body), zlib.crc32(body)) + body


def view(d):
    return d.arrangement()[0]


def visible(v, prefix):
    live = v[prefix + '_live']
    return live >= 0 and (v[prefix + '_pinned'] or
                         v[prefix + '_first'] <= live < v[prefix + '_first'] + v[prefix + '_count'])


def checks():
    r = Report()
    d = open_device()
    try:
        d.wait_ready(); stop_for_test(d)
        d.upload_fixture(fixture()); d, _ = reopen(d)
        d.key('4', 'f')
        for _ in range(5): d.key('.')
        d.key('enter')
        wait_state(d, song_entry=7, playing=1)
        v = view(d)
        r.check('song scrolls across list page boundary', v['song_follow'] and v['song_first'] >= 6 and visible(v, 'song'))
        r.check('playback keeps edit row unchanged', v['edit_row'] == 5)
        for _ in range(5): d.key(';')
        v = view(d)
        r.check('browsing pins distant playing row', v['edit_row'] == 0 and v['song_pinned'] and visible(v, 'song'))
        d.key('f'); v = view(d)
        r.check('F resumes following without moving edit row', v['song_follow'] and v['edit_row'] == 0 and visible(v, 'song'))
        d.key('3', 'f')
        for _ in range(8):
            time.sleep(.2)
            v = view(d)
            r.check('Scenes follows actual nonsequential song scene', v['scene_follow'] and visible(v, 'scene'))
        stop_for_test(d)
        d.key('enter'); wait_state(d, playing=1, scene=0, song_mode=0)
        for _ in range(15): d.key('.')
        d.key('enter')
        wait_state(d, queued=16, scene=0)
        v = view(d)
        r.check('queued scene does not hide the currently playing scene', v['scene_live'] == 0 and v['scene_pinned'] and visible(v, 'scene'))
        wait_state(d, scene=15, queued=0)
        d.key('f'); v = view(d)
        r.check('scene launch moves follow view to last scene', v['scene_live'] == 15 and v['scene_follow'] and visible(v, 'scene'))
        r.check('scene loop has no false song playhead', v['song_live'] == -1)
        stop_for_test(d)
        d.key('4', 'f')
        for _ in range(63): d.key('.')
        d.key('enter'); wait_state(d, song_entry=63, playing=1)
        v = view(d)
        r.check('last song entry stays visible', v['song_live'] == 63 and visible(v, 'song'))
        d.level(150)  # Measures the enforced 5% PCM ceiling.
        wait_state(d, playing=0)
        v = view(d)
        r.check('natural stop clears playhead and retains edit row', v['song_live'] == -1 and v['scene_live'] == -1 and v['edit_row'] == 63)
        d.key('space'); wait_state(d, song_entry=63, playing=1)
        r.check('restart follows last selected row', view(d)['song_follow'] == 1)
        stop_for_test(d)
        print(f'{r.failures} failures')
        return bool(r.failures)
    finally:
        os.close(d.fd)


if __name__ == '__main__':
    with isolated_project():
        raise SystemExit(checks())
