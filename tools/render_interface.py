#!/usr/bin/env python3
"""Rasterize device Canvas commands with its font and RGB332 palette.

Usage: python3 tools/render_interface.py build/interface.txt docs/images
Run `make render-interface` to generate the capture and images together.
"""
from pathlib import Path
import re
import struct
import sys
import zlib

SCREENS = (
    'performance', 'drums', 'piano-roll', 'sound-editor', 'mixer', 'song',
    'scenes', 'sampler-files', 'sampler-cut', 'sampler-pads', 'menu', 'projects',
    'quick-start', 'mixer-fft', 'visual-spectrum', 'visual-wave',
    'visual-waterfall', 'visual-orbit', 'visual-settings',
)
ROOT = Path(__file__).resolve().parent.parent
FONT = ROOT / 'firmware/managed_components/m5stack__m5gfx/src/lgfx/Fonts/glcdfont.h'


def render(frames, destination, font):
    columns = min(3, len(frames))
    width = columns * 250 - 10
    height = ((len(frames) + columns - 1) // columns) * 145 - 10
    pixels = bytearray([35, 35, 35]) * (width * height)

    def put(x, y, color):
        if 0 <= x < width and 0 <= y < height:
            offset = (y * width + x) * 3
            pixels[offset:offset + 3] = color

    def rgb(value):
        return bytes((((value >> 13) & 7) * 255 // 7,
                      ((value >> 8) & 7) * 255 // 7,
                      ((value >> 3) & 3) * 255 // 3))

    for index, frame in enumerate(frames):
        ox, oy = index % columns * 250, index // columns * 145
        for command in frame.splitlines():
            if command.startswith('R '):
                _, x, y, w, h, color = command.split()
                x, y, w, h = map(int, (x, y, w, h))
                color = rgb(int(color))
                for yy in range(h):
                    for xx in range(w):
                        put(ox + x + xx, oy + y + yy, color)
            elif command.startswith('T '):
                _, x, y, color, text = command.split(' ', 4)
                x, y, color = int(x), int(y), rgb(int(color))
                for i, char in enumerate(text):
                    if ord(char) * 5 + 4 >= len(font):
                        continue
                    for xx in range(5):
                        bits = font[ord(char) * 5 + xx]
                        for yy in range(8):
                            if bits & (1 << yy):
                                put(ox + x + i * 6 + xx, oy + y + yy, color)
    raw = b''.join(
        b'\0' + b''.join(bytes(pixels[(y * width + x) * 3:(y * width + x) * 3 + 3]) * 2
                         for x in range(width))
        for y in range(height) for _ in range(2)
    )

    def chunk(name, data):
        return struct.pack('>I', len(data)) + name + data + struct.pack('>I', zlib.crc32(name + data))

    destination.write_bytes(
        b'\x89PNG\r\n\x1a\n'
        + chunk(b'IHDR', struct.pack('>IIBBBBB', width * 2, height * 2, 8, 2, 0, 0, 0))
        + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
    )


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    if not FONT.is_file():
        raise SystemExit('Build firmware first to obtain the M5GFX device font.')
    data = FONT.read_text().split('PROGMEM = {', 1)[1].split('};', 1)[0]
    font = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]+)', data))
    frames = re.split(r'^FRAME \d+\n', Path(sys.argv[1]).read_text(), flags=re.M)[1:]
    if len(frames) != len(SCREENS):
        raise SystemExit(f'Expected {len(SCREENS)} frames, got {len(frames)}')
    destination = Path(sys.argv[2])
    destination.mkdir(parents=True, exist_ok=True)
    for name, frame in zip(SCREENS, frames):
        render([frame], destination / f'{name}.png', font)
    for name, indices in [('interface', range(6)), ('sampler-projects', range(6, 12)),
                          ('audio-visuals', range(13, 19))]:
        render([frames[i] for i in indices], destination / f'{name}.png', font)
    print(f'Rendered {len(frames)} screens and 3 galleries in {destination}')


if __name__ == '__main__':
    main()
