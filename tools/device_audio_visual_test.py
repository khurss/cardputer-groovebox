#!/usr/bin/env python3
"""Validate real audio analysis, visual modes, bare arrows and RGB at 5% output."""
import os
from pathlib import Path
import time
from device_test import Report, isolated_project, open_device, parse_fields, stop_for_test
from device_menu_test import page, row, state
from device_synth_test import reopen


def visual(device):
    device.send('B')
    return parse_fields(device.wait_for('VIS '))


def checks():
    r = Report()
    d = open_device()
    try:
        d.wait_ready()
        stop_for_test(d)
        d.key('2', 'f')
        r.check('synth opens in Performance', visual(d)['performance'] == 1)
        d.down('z')
        time.sleep(.4)
        first = visual(d)
        r.check('live instrument reaches real FFT and RGB', first['peak'] > 0 and first['bin'] > 0 and first['rgb'] > 0)
        d.up('z')
        time.sleep(.7)
        d.down('e')
        time.sleep(.4)
        second = visual(d)
        r.check('different notes change FFT frequency and LED color', second['bin'] != first['bin'] and second['rgb'] != first['rgb'])
        d.key('-')  # MAIN 0: post-master analysis must follow actual silence.
        time.sleep(.4)
        silent = visual(d)
        r.check('master zero makes analyzer and LED silent', silent['peak'] == 0 and silent['rgb'] == 0)
        d.up('e')
        d.key('+')
        d.key('esc')
        r.check('Esc returns to piano roll', visual(d)['performance'] == 0)
        pitch = state(d)['focus_pitch']
        d.key(';')
        r.check('piano roll arrow works without Fn', state(d)['focus_pitch'] > pitch)
        d.key('7', 'f')
        r.check('Fn+7 opens FFT mixer', state(d)['screen'] == 6)
        d.key('/')
        d.key('.')
        r.check('bare arrows adjust selected mixer channel', d.arrangement()[3][0]['volume'] == 95)
        d.key('m')
        r.check('FFT mixer mute shares normal mixer state', d.arrangement()[3][0]['mute'] == 1)
        stop_for_test(d)
        demo = Path(__file__).resolve().parent.parent / 'examples/projects/neon-drive.gbp'
        d.upload_fixture(demo.read_bytes())
        d, _ = reopen(d)
        d.key('4', 'f')
        for _ in range(12):
            d.key('.')  # chorus row, no Fn
        d.key('enter')
        time.sleep(.3)
        d.send('C reset')
        d.wait_for('CPU ')
        before = d.cpu()
        d.key('8', 'f')
        colors = set()
        for mode in range(4):
            r.check(f'visual mode {mode} selected', visual(d)['mode'] == mode)
            first_frame = visual(d)['frames']
            for _ in range(12):
                time.sleep(.25)
                v = visual(d)
                colors.add(v['rgb'])
                if v['peak'] > 1557:
                    raise RuntimeError('analysis observed output above the 5% ceiling')
            r.check(f'visual mode {mode} receives ongoing audio', visual(d)['frames'] - first_frame >= 40)
            d.key('/')
        d.key('p')
        d.key(';')
        v = visual(d)
        r.check('bare controls configure palette and sensitivity', v['palette'] == 1 and v['gain'] == 4)
        d.key('7', 'f')
        time.sleep(2)
        after = d.cpu()
        r.check('FFT and display fit timing budget', v['fft_max_us'] < 15000 and after['ui_frame_max_us'] < 50000)
        r.check('audio DSP stays below 50 percent', after['load_permille'] < 500)
        r.check('no audio or queue failures', all(after[k] == before[k] for k in ('tx_overflows', 'rx_overflows', 'failures', 'event_overflows')))
        r.check('RGB transport works with varied song colors', len(colors - {0}) > 2 and v['rgb_ready'] == 1 and v['rgb_failures'] == 0)
        print('INFO visual', v)
        print('INFO audio/display', after)
        d.level(100)
        d.key('8', 'f')
        d.key('opt'); page(d, 2); row(d, 8); d.key('enter')
        for _ in range(4):
            d.key(',')
        d.key('esc'); d.key('opt')
        time.sleep(.2)
        r.check('RGB can be disabled during playback', visual(d)['led'] == 0 and visual(d)['rgb'] == 0)
        stop_for_test(d)
        deadline = time.monotonic() + 6
        while visual(d)['peak'] and time.monotonic() < deadline:
            time.sleep(.1)
        r.check('analysis settles to silence after synth release', visual(d)['peak'] == 0)
        print(f'{r.failures} failures')
        return bool(r.failures)
    finally:
        os.close(d.fd)


if __name__ == '__main__':
    with isolated_project():
        raise SystemExit(checks())
