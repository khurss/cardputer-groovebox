#!/usr/bin/env python3
"""Exercise the persistent menu through keyboard events at the 5% test ceiling."""
import os
import time
from device_test import Report, isolated_project, open_device, stop_for_test
from device_sampler_test import wait_sampler
from device_arrangement_test import wait_state
from device_synth_test import reopen


def state(device):
    return device.state()[0]


def page(device, target):
    for _ in range(3):
        if state(device)['menu_page'] == target:
            return
        device.key('tab')
    raise RuntimeError('menu section did not change')


def row(device, target):
    before = state(device)['menu_row']
    for _ in range(abs(target-before)):
        device.key('.' if target > before else ';', 'f')
    if state(device)['menu_row'] != target:
        raise RuntimeError('menu row did not change')


def checks():
    report = Report()
    d = open_device()
    try:
        d.wait_ready()
        stop_for_test(d)
        report.check('test volume and ceiling are 5 percent', state(d)['master'] == 5 and state(d)['output_limit'] == 5)
        d.key('fn')
        report.check('Fn tap no longer opens an overlay', state(d)['menu'] == 0)
        d.key('opt')
        report.check('Opt opens persistent menu', state(d)['menu'] == 1)
        d.key('.', 'f')
        report.check('physical Fn arrow keeps menu open', state(d)['menu'] == 1 and state(d)['menu_row'] == 1)
        d.key('enter', 'f')
        report.check('Enter opens selected synth screen', state(d)['screen'] == 1 and state(d)['menu'] == 0)
        d.down('z')
        d.level(100)
        d.key('opt')
        d.key('q')
        d.up('z')
        time.sleep(0.3)
        report.check('notes release with menu open', d.cpu()['active_voices'] == 0)
        page(d, 2)
        row(d, 1)
        d.key('enter')
        d.key('/', 'f')
        report.check('tempo editable without a memorized hotkey', state(d)['bpm'] == 121 and state(d)['menu_edit'] == 1)
        d.key('back')
        report.check('Back leaves value editing first', state(d)['menu'] == 1 and state(d)['menu_edit'] == 0)
        d.key('-')
        report.check('global volume still works inside menu', state(d)['master'] == 0)
        d.key('=', 's')
        report.check('printed plus restores 5 percent', state(d)['master'] == 5)
        row(d, 10)
        d.key('enter')
        d.key('/', 'f')
        report.check('sound waveform editable from settings list', d.instrument()[0]['wave'] == 3)
        d.key(',', 'f')
        d.key('enter')
        page(d, 1)
        row(d, 0)
        d.key('enter')
        report.check('play action starts transport', wait_state(d, playing=1)['menu'] == 0)
        stop_for_test(d)
        d.key('1', 'f')
        d.key('q')
        d.key('opt')
        page(d, 1)
        row(d, 4)
        d.key('enter')
        report.check('clear action removes drum notes', d.state()[1][0][0] == '.')
        row(d, 1)
        d.key('enter')
        report.check('undo action restores notes', d.state()[1][0][0] == 'x')
        d.key('6', 'f')
        wait_sampler(d, busy=0)
        d.key('opt')
        page(d, 1)
        row(d, 0)
        d.key('enter')
        wait_sampler(d, busy=0)
        report.check('browse action opens sampler files', state(d)['screen'] == 5 and state(d)['menu'] == 0)
        d.key('opt')
        row(d, 1)
        d.key('enter')
        wait_sampler(d, recording=1)
        time.sleep(0.4)
        d.key('opt')
        d.key('enter')
        recorded, _, _ = wait_sampler(d, recording=0, busy=0)
        report.check('microphone starts and stops from the menu', recorded['source'].startswith('REC') and recorded['source_frames'] > 0)
        time.sleep(3.5)
        d, _ = reopen(d)
        report.check('menu settings survive restart', state(d)['bpm'] == 121 and state(d)['menu'] == 0)
        report.check('volume remains at 5 percent after restart', state(d)['master'] == 5 and state(d)['output_limit'] == 5)
        print('INFO menu CPU', d.cpu())
        print(f'{report.failures} failures')
        return 1 if report.failures else 0
    finally:
        os.close(d.fd)


if __name__ == '__main__':
    with isolated_project():
        raise SystemExit(checks())
