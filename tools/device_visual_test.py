#!/usr/bin/env python3
"""ESC, animated rendering, demo installation and chorus playback at 5%."""
import os
import time
from device_test import Report, isolated_project, open_device, stop_for_test
from device_menu_test import page, row, state
from device_ux_test import projects, project, project_row, switched
from device_arrangement_test import wait_state


def checks():
    r = Report()
    d = open_device()
    try:
        d.wait_ready(); stop_for_test(d)
        d.key('`')
        r.check('physical Escape mapping opens menu', state(d)['menu'] == 1)
        d.key('esc')
        r.check('Escape closes menu', state(d)['menu'] == 0)
        d.key('2', 'f'); d.key('opt'); page(d, 1); row(d, 7); d.key('enter')
        d.down('z'); d.down('a'); d.down('q')
        d.send('C reset'); d.wait_for('CPU ')
        before = d.cpu()
        for key in ('x', 'c', 'v', 'b', 'n', 'm'):
            d.down(key); time.sleep(.25); d.up(key); time.sleep(.25)
        after = d.cpu()
        r.check('performance animation keeps drawing', after['ui_frames'] >= 35)
        r.check('animation frames fit 50ms budget', after['ui_frame_max_us'] < 50000)
        r.check('animation has no new audio failures', all(after[k] == before[k] for k in ('tx_overflows', 'rx_overflows', 'failures', 'event_overflows')))
        print('INFO animated performance', after)
        d.key('esc')
        for key in ('z', 'a', 'q'): d.up(key)
        time.sleep(.6)
        r.check('Escape returns from performance and releases notes', state(d)['menu'] == 0 and d.cpu()['active_voices'] == 0)
        projects(d); project_row(d, 3); d.key('enter'); time.sleep(.5)
        _, lines = project(d)
        files = [x[5:] for x in lines if x.startswith('FILE ')]
        demos = ('neon-drive', 'sunset-arcade', 'midnight-run', 'circuit-pressure',
                 'concrete-pulse', 'night-transit', 'liquid-signal', 'steel-rush', 'redline-foundry',
                 'shadow-foundry', 'black-neon')
        r.check('all eleven demos installed', all(name in files for name in demos))
        for name in demos:
            project_row(d, 4 + files.index(name)); d.key('enter'); d.key('enter'); d = switched(d)
            r.check(name + ' loads at 5 percent', name in project(d)[0] and state(d)['master'] == 5 and state(d)['output_limit'] == 5)
            d.key('4', 'f')
            for _ in range(12): d.key('.', 'f')
            d.key('enter'); wait_state(d, playing=1)
            time.sleep(.2)
            d.send('C reset'); d.wait_for('CPU '); before = d.cpu()
            for screen in ('1', '2', '3', '4', '5', '6', '7', '8'):
                d.key(screen, 'f'); time.sleep(.6)
            after = d.cpu()
            r.check(name + ' chorus stays within DSP budget', after['load_permille'] < 500)
            r.check(name + ' no new audio errors across screens', all(after[k] == before[k] for k in ('tx_overflows', 'rx_overflows', 'failures', 'event_overflows')))
            print('INFO', name, after)
            stop_for_test(d)
            projects(d)
            _, lines = project(d); files = [x[5:] for x in lines if x.startswith('FILE ')]
        d.key('esc')
        print(f'{r.failures} failures')
        return bool(r.failures)
    finally:
        os.close(d.fd)


if __name__ == '__main__':
    with isolated_project():
        raise SystemExit(checks())
