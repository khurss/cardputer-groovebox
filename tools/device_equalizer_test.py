#!/usr/bin/env python3
"""Measure EQ changes in actual output PCM and saved projects, at a 5% ceiling."""
import os
import struct
import time
import zlib
from device_test import Report, isolated_project, open_device, parse_fields, stop_for_test
from device_synth_test import stress_project, reopen
from device_menu_test import page, row


def fixture():
    body = bytearray(stress_project(120)[14:])
    for pattern in range(16):
        body[42+pattern*130:170+pattern*130] = bytes(128)
    for track in range(4):
        for pattern in range(16): body[2122+(track*16+pattern)*387] = 0
    # A sustained, quiet B5 sine (987.77 Hz), close to the 1 kHz EQ band.
    body[2120:2123] = bytes((16, 0, 1))
    struct.pack_into('<HHBB', body, 2123, 0, 1536, 83, 100)
    body[27097:27106] = bytes((0, 0, 0, 127, 0, 127, 0, 0, 10))
    return b'GBPJ' + struct.pack('<HII', 2, len(body), zlib.crc32(body)) + body


def eq(d):
    d.send('E')
    return parse_fields(d.wait_for('EQ '))


def level(d):
    d.send('L 400')
    values = parse_fields(d.wait_for('LEVEL '))
    if values['output_peak'] > 1557: raise RuntimeError('output exceeds 5% ceiling')
    return values['output_power']


def checks():
    r=Report(); d=open_device()
    try:
        d.wait_ready(); stop_for_test(d)
        d.upload_fixture(fixture()); d,_=reopen(d)
        d.key('7','f')
        r.check('old project opens with flat enabled EQ', eq(d)['enabled'] == 1 and all(eq(d)[f'g{i}'] == 0 for i in range(7)))
        for _ in range(3): d.key('/')
        d.key('space'); time.sleep(.3)
        flat=level(d)
        for _ in range(4): d.key(';','s')
        boost=level(d)
        d.key('b'); bypass=level(d)
        d.key('b')
        for _ in range(8): d.key('.','s')
        cut=level(d)
        r.check('1 kHz boost changes actual output power', flat > 0 and 12*flat < boost < 20*flat)
        r.check('bypass restores dry output', .85*flat < bypass < 1.15*flat)
        r.check('1 kHz cut reduces actual output power', cut < .10*flat)
        print('INFO PCM powers', {'flat':flat,'boost':boost,'bypass':bypass,'cut':cut})
        d.key('-'); r.check('MAIN zero still mutes with EQ', level(d) == 0);d.key('+')
        d.key('0');r.check('0 resets the selected band', eq(d)['g3'] == 0)
        d.key('z','f');r.check('undo restores band gain', eq(d)['g3'] == -12)
        d.key(',');d.key(';','s')
        d.key('0','s');r.check('Shift 0 resets all bands', all(eq(d)[f'g{i}'] == 0 for i in range(7)))
        d.key('z','f');r.check('undo restores complete EQ', eq(d)['g2'] == 3 and eq(d)['g3'] == -12)
        d.key('b');saved=eq(d)
        stop_for_test(d);time.sleep(3.4)
        d,_=reopen(d);loaded=eq(d)
        r.check('gains and bypass survive project save/reboot', all(loaded[k] == saved[k] for k in ['enabled',*[f'g{i}' for i in range(7)]]))
        d.key('7','f');d.key('opt');page(d,2);row(d,3);d.key('enter');d.key('/');d.key('esc');d.key('opt')
        r.check('menu chooses EQ frequency band', eq(d)['band'] == 1)
        d.key('opt');page(d,1);row(d,2);d.key('enter');d.key('opt')
        r.check('menu reset clears gains and retains bypass', all(eq(d)[f'g{i}'] == 0 for i in range(7)) and eq(d)['enabled'] == 0)
        print(f'{r.failures} failures');return bool(r.failures)
    finally: os.close(d.fd)


if __name__=='__main__':
    with isolated_project():
        raise SystemExit(checks())
