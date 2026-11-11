#!/usr/bin/env python3
"""Audit ES8311 gain and MAIN scaling; audible checks always have a 5% ceiling."""
import os
import time
from device_test import Report, isolated_project, open_device, parse_fields, stop_for_test
from device_equalizer_test import fixture, level
from device_synth_test import reopen


def output_config(d):
    d.send('Q')
    return parse_fields(d.wait_for('AUDIO '))


def checks():
    r = Report()
    d = open_device()
    try:
        d.wait_ready()
        stop_for_test(d)
        # Keep the 1% measurement above integer PCM/power quantization noise.
        d.upload_fixture(fixture(synth_level=40))
        d, _ = reopen(d)
        config = output_config(d)
        print('INFO codec', config)
        r.check('codec is at unity gain, with no DAC mute or DRC attenuation',
                config['read_ok'] == 1 and config['dac_volume'] == 0xBF and
                not config['dac_control'] & 0x60 and not config['dac_drc'] & 0x80 and
                config['dac_eq'] & 0x08)
        r.check('audible checks have an independent 5 percent ceiling',
                config['test_mode'] == 1 and config['master'] == 5 and config['output_limit'] == 5)
        d.key('space')
        time.sleep(.3)
        at_five = level(d)
        for _ in range(4): d.key('-', 'c')
        at_one = level(d)
        r.check('MAIN 1 to 5 percent scales signal amplitude, not a stuck volume',
                at_one > 0 and 20 * at_one < at_five < 35 * at_one)
        d.key('-', 'c')
        r.check('MAIN zero is digital silence', level(d) == 0)
        d.key('+')
        for _ in range(19): d.key('+')
        capped = level(d)
        config = output_config(d)
        r.check('test mode caps MAIN 100 at the same measured 5 percent output',
                config['master'] == 100 and config['output_limit'] == 5 and
                .9 * at_five < capped < 1.1 * at_five)
        print('INFO PCM powers', {'main_1': at_one, 'main_5': at_five, 'main_100_test_limit_5': capped})
        stop_for_test(d)
        print(f'{r.failures} failures')
        return bool(r.failures)
    finally:
        os.close(d.fd)


if __name__ == '__main__':
    # Read the ordinary limit without starting playback or changing the project.
    d = open_device()
    try:
        d.wait_ready()
        config = output_config(d)
        print('INFO normal output', config)
        if config['test_mode'] != 0 or config['output_limit'] != 100:
            raise RuntimeError('normal session unexpectedly has an output cap or active test mode')
    finally:
        os.close(d.fd)
    with isolated_project():
        raise SystemExit(checks())
