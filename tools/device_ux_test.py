#!/usr/bin/env python3
"""Projects, guide, string legato and recording, isolated at a 5% ceiling."""
import os
import time
from device_test import Report, isolated_project, open_device, stop_for_test
from device_menu_test import page, row, state
from device_synth_test import wait_recording


def project(d):
    d.send('P')
    header = d.wait_for('PROJECT name=')
    lines = []
    while True:
        line = d.wait_for('')
        if line == 'PROJECT-END':
            return header, lines
        lines.append(line)


def projects(d):
    d.key('opt')
    page(d, 0)
    row(d, 6)
    d.key('enter')
    time.sleep(.1)


def project_row(d, n):
    for _ in range(40):
        d.key(';', 'f')
    for _ in range(n):
        d.key('.', 'f')


def named(d, n, name, switch):
    project_row(d, n)
    d.key('enter')
    for ch in name:
        d.key(ch)
    d.key('enter')
    if switch:
        return switched(d)
    time.sleep(.3)
    return d


def switched(d):
    try:
        d.wait_for('PROJECT open=', timeout=10)
    except TimeoutError:
        print('DIAGNOSTIC', project(d), state(d), d.cpu())
        raise
    os.close(d.fd)
    time.sleep(1)
    d = open_device()
    d.wait_ready()
    stop_for_test(d)
    return d


def run():
    r = Report()
    d = open_device()
    try:
        d.wait_ready()
        stop_for_test(d)
        d.key('2', 'f')
        d.key('opt')
        page(d, 1)
        row(d, 7)
        d.key('enter')
        r.check('Performance enables string layout', d.instrument()[0]['layout'] == 1)
        d.down('z')
        d.level(100)
        d.down('x')
        d.level(100)
        r.check('same row uses one voice', d.cpu()['active_voices'] == 1 and state(d)['focus_pitch'] == 62)
        d.up('x')
        r.check('pull-off returns to held note', state(d)['focus_pitch'] == 60)
        d.down('a'); d.down('q'); d.down('1')
        d.level(200)
        r.check('four strings form a chord', d.cpu()['active_voices'] == 4)
        d.send('C reset'); d.wait_for('CPU ')
        baseline = d.cpu()
        d.level(3000)
        live_cpu = d.cpu()
        r.check('four-string DSP stays within budget', live_cpu['load_permille'] < 500 and live_cpu['tx_overflows'] == baseline['tx_overflows'] and live_cpu['rx_overflows'] == baseline['rx_overflows'])
        print('INFO live strings CPU', live_cpu)
        d.key('opt')
        for k in ('z', 'a', 'q', '1'):
            d.up(k)
        time.sleep(.6)
        r.check('menu never strands a string', d.cpu()['active_voices'] == 0)
        d.key('opt')
        d.key('alt'); d.key('space'); wait_recording(d, 1)
        d.down('z'); time.sleep(.15); d.down('x'); time.sleep(.15)
        d.up('x'); time.sleep(.15); d.up('z')
        d.key('alt'); wait_recording(d, 0)
        notes = d.instrument()[1]
        r.check('legato records three segments', [n['pitch'] for n in notes] == [60, 62, 60])
        stop_for_test(d)
        d.key('opt'); page(d, 0); row(d, 7); d.key('enter')
        r.check('guide opens explicitly', state(d)['help'] == 1)
        for _ in range(9):
            d.key('enter')
        d.key('opt')
        r.check('guide closes', state(d)['help'] == 0)
        suffix = str(int(time.time()))[-7:]
        first, copy, blank = 'ux'+suffix, 'copy'+suffix, 'new'+suffix
        projects(d)
        d = named(d, 1, first, True)
        r.check('new project loaded at five percent', first in project(d)[0] and state(d)['master'] == 5 and state(d)['output_limit'] == 5)
        d.key('q')
        projects(d)
        d = named(d, 2, copy, False)
        r.check('save copy keeps current project', first in project(d)[0])
        d = named(d, 1, blank, True)
        r.check('new project starts with empty steps', d.state()[1][0][0] == '.')
        projects(d)
        _, lines = project(d)
        files = [line[5:] for line in lines if line.startswith('FILE ')]
        project_row(d, 3 + files.index(first))
        d.key('enter'); d.key('back')
        r.check('open confirmation can be cancelled', blank in project(d)[0])
        d.key('enter'); d.key('enter')
        d = switched(d)
        r.check('opening restores saved notes', first in project(d)[0] and d.state()[1][0][0] == 'x')
        projects(d); project_row(d, 0); d.key('enter'); time.sleep(.3)
        r.check('explicit save reports success', 'MESSAGE Project saved' in project(d)[1])
        d.key('opt')
        print('INFO CPU', d.cpu())
        print(f'{r.failures} failures')
        return bool(r.failures)
    finally:
        os.close(d.fd)


if __name__ == '__main__':
    with isolated_project():
        raise SystemExit(run())
