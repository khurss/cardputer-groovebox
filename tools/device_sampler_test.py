#!/usr/bin/env python3
"""Real SD import/cut/pads/scene/mixer/live microphone checks at a hard 5% ceiling."""
import io
import math
import os
import struct
import time
import wave
from device_test import Report, isolated_project, open_device, stop_for_test, parse_fields
from device_synth_test import reopen


def source_wav():
    stream = io.BytesIO()
    with wave.open(stream, 'wb') as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(16000)
        data = bytearray()
        for frame in range(32000):
            frequency = (220, 330, 440, 660)[frame // 8000]
            value = math.sin(2 * math.pi * frequency * frame / 16000)
            data += struct.pack('<hh', int(value * 18000), int(value * 6000))
        out.writeframes(data)
    return stream.getvalue()


def wait_sampler(device, **expected):
    deadline = time.monotonic() + 90
    while True:
        state, slices, pads = device.sampler()
        if all(state.get(k) == v for k, v in expected.items()):
            return state, slices, pads
        if time.monotonic() >= deadline:
            raise TimeoutError(f'sampler did not reach {expected}: {state}')
        time.sleep(0.03)


def checks():
    report = Report()
    device = open_device()
    try:
        device.wait_ready()
        wait_sampler(device, ready=1, busy=0)
        fields, _ = device.state()
        report.check('test master and hard ceiling are 5 percent', fields['master'] == 5 and fields['output_limit'] == 5)
        device.upload_wav(source_wav())
        device.key('6', 'f')
        device.key('b', 'f')
        wait_sampler(device, busy=0)
        device.key('enter')
        state, _, _ = wait_sampler(device, busy=0, source_frames=64000)
        report.check('SD stereo WAV converted from 16 to 32 kHz', state['source'] == 'fixture.wav')
        device.key('/', 'f')
        device.key('a')
        device.key('/', 'f')
        device.key('/', 'f')
        device.key('d')
        state, _, _ = device.sampler()
        report.check('manual start and end follow waveform cursor', 0 < state['start'] < state['end'] < 64000)
        device.key('r')
        wait_sampler(device, busy=0)
        device.key('enter', 'f')
        _, slices, pads = wait_sampler(device, busy=0)
        report.check('automatic chop creates four exact slices', [(s['start'], s['end']) for s in slices] == [(0, 16000), (16000, 32000), (32000, 48000), (48000, 64000)])
        report.check('chops assigned to first four pads', [p['slot'] for p in pads[:4]] == list(range(4)))
        quiet = device.level(150)
        device.key('1')
        report.check('flash sample preview is audible at 5 percent', device.level(250) > quiet)
        time.sleep(0.3)
        device.key('p', 'f')
        for pad, step in zip('1234', 'qwer'):
            device.key(pad)
            device.key(step)
        _, _, pads = device.sampler()
        report.check('independent sample sequencer receives four steps', all(pads[i]['steps'][i] == 'x' for i in range(4)))
        report.check('drum sequencer remains empty', all(set(row) == {'.'} for row in device.state()[1].values()))
        device.key('space')
        report.check('sample pattern plays in a scene', device.level(2400) > quiet)
        device.key('5', 'f')
        for _ in range(6):
            device.key('/', 'f')
        device.key('m')
        report.check('sampler has an independent mixer mute', device.level(300) == 0)
        device.key('m')
        report.check('sampler mixer unmute restores sound', device.level(2400) > quiet)
        device.key('6', 'f')
        initial_cpu = device.cpu()
        device.key('alt')
        wait_sampler(device, recording=1, busy=1)
        time.sleep(2.2)
        device.send('L 300')
        mic = parse_fields(device.wait_for('LEVEL '))
        report.check('microphone ADC delivers real nonzero signal', mic['peak'] > 0 and mic['power'] > 0)
        report.check('live recording output remains below five percent', mic['output_peak'] <= 1557)
        during, _, _ = device.sampler()
        report.check('microphone captures while scene keeps playing', during['frames'] >= 64000 and device.state()[0]['playing'] == 1)
        device.key('alt')
        state, slices, _ = wait_sampler(device, recording=0, busy=0)
        report.check('microphone WAV finalized and shown as waveform', state['source'].startswith('REC') and state['source_frames'] >= 64000)
        report.check('microphone ring did not drop blocks', state['dropped'] == 0)
        after_cpu = device.cpu()
        for counter in ('tx_overflows', 'rx_overflows', 'failures', 'event_overflows'):
            report.check('live SD recording no new ' + counter, after_cpu[counter] == initial_cpu[counter])
        device.key('enter')
        state, unchanged, _ = wait_sampler(device, busy=0)
        report.check('cache writes blocked during playback without losing cuts', len(unchanged) == len(slices) and 'Stop playback' in state['message'])
        device.key('space')
        device.key('enter')
        state, slices, _ = wait_sampler(device, busy=0)
        recorded = slices[-1]
        report.check('recorded audio can become another playable sample', len(slices) == 5 and recorded['source'].startswith('REC'))
        device.key('4')
        report.check('microphone WAV plays real recorded PCM', device.level(3000) > 0)
        # Save while stopped, then prove v4 and the actual source WAV survive restart.
        time.sleep(3.5)  # SAVE can arrive while a sampler status request is being consumed.
        device, boot = reopen(device)
        _, reopened, pads = wait_sampler(device, ready=1, busy=0)
        report.check('v4 sample clips survive restart', reopened == slices)
        device.key('6', 'f')
        device.key('p', 'f')
        _, _, restored_pads = device.sampler()
        report.check('sample pattern survives restart', all(restored_pads[i]['steps'][i] == 'x' for i in range(4)))
        device.key('1')
        report.check('cached sample plays after restart', device.level(200) > 0)
        time.sleep(0.4)
        device.key(']', 'f')
        report.check('selecting sampler pattern updates the scene immediately', device.arrangement()[1][0]['t5'] == 1)
        device.key('q')
        device.key('[', 'f')
        _, _, first_pattern = device.sampler()
        report.check('sample patterns remain independent', all(first_pattern[i]['steps'][i] == 'x' for i in range(4)))
        device.key('d', 'f')
        _, remaining, pads = wait_sampler(device, busy=0)
        report.check('deleting a slice frees only its slot', len(remaining) == 4 and all(s['slot'] != 0 for s in remaining))
        device.key('1')
        report.check('deleted sample cannot play stale cached audio', device.level(250) == 0)
        time.sleep(3.5)
        device, _ = reopen(device)
        _, final_slices, _ = wait_sampler(device, ready=1, busy=0)
        report.check('slice deletion persists through cache reload', final_slices == remaining)
        print('INFO sampler CPU', after_cpu)
        print(f'{report.failures} failures')
        return 1 if report.failures else 0
    finally:
        os.close(device.fd)


def stress_project():
    from device_synth_test import stress_project as synth_project
    import zlib
    old = synth_project(240)[14:]
    body = bytearray(old[:8] + b"\x01" + old[8:26888])
    for scene in range(16):
        body += old[26888 + scene*5:26888 + (scene+1)*5]
        body += bytes((0 if scene == 0 else 255,))
    body += old[26968:]
    body += bytes((5,)) + bytes((100, 0, 0))*6 + bytes((0, 2, 1, 50))*4
    for slot in range(32):
        body += (b"fixture.wav".ljust(48, b"\0") + struct.pack('<II', slot*16000, (slot+1)*16000)) if slot < 4 else bytes(56)
    for pattern in range(16):
        body += bytes((16, 2)) + bytes((127 if pattern == 0 else 0,))*128 + bytes((0, 1, 2, 3, 0, 1, 2, 3))
    return b'GBPJ' + struct.pack('<HII', 4, len(body), zlib.crc32(body)) + body


def run_stress(seconds=60):
    with isolated_project():
        device = open_device()
        try:
            device.wait_ready()
            wait_sampler(device, ready=1, busy=0)
            device.upload_wav(source_wav())
            device.upload_fixture(stress_project())
            device, _ = reopen(device)
            wait_sampler(device, ready=1, busy=0)
            report = Report()
            device.key('space')
            time.sleep(1.2)
            device.send('C reset')
            device.wait_for('CPU ')
            before = device.cpu()
            device.key('6', 'f')
            device.key('alt')
            wait_sampler(device, recording=1)
            print('INFO dense synth + eight drums + four sample voices + SD microphone:', seconds, 'seconds')
            deadline = time.monotonic() + seconds
            while time.monotonic() < deadline:
                time.sleep(min(1, max(0, deadline-time.monotonic())))
            device.level(500)  # Checks the actual output stays at or below five percent.
            after = device.cpu()
            state, _, _ = device.sampler()
            report.check('combined audio load stays within 50 percent', after['load_permille'] <= 500, str(after['load_permille']/10) + '%')
            report.check('mixed synth voice budget exercised', after['voice_budget'] == 4 and after['peak_voices'] == after['voice_budget'])
            report.check('all four sample voices exercised', state['voices'] == 4)
            report.check('long live recording has no dropped blocks', state['dropped'] == 0 and state['frames'] >= seconds * 32000)
            for counter in ('tx_overflows', 'rx_overflows', 'failures', 'event_overflows'):
                report.check('combined stress no new ' + counter, after[counter] == before[counter])
            device.key('alt')
            wait_sampler(device, recording=0, busy=0)
            stop_for_test(device)
            print('INFO stress CPU', after)
            print(f'{report.failures} failures')
            return 1 if report.failures else 0
        finally:
            os.close(device.fd)


def main():
    with isolated_project():
        return checks()


if __name__ == '__main__':
    import sys
    raise SystemExit(run_stress() if '--stress' in sys.argv else main())
