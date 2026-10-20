#!/usr/bin/env python3
"""Generate the rotated-numeral glyph atlas used by the watchface.

The watch rotates two-digit labels around the dial at runtime. To keep them
sharp at any angle, it samples a 1-bit atlas rendered at several times the
largest on-screen size and antialiases the result while drawing.

Usage: python3 tools/gen_digits.py [--check]

Output format (little endian), app/resources/data/digits.bin:
  char[4]  magic "RD01"
  u8       glyph count (10, digits 0-9)
  u8       reserved (0)
  u16      glyph height in atlas pixels
  10 x { u16 width, u16 stride_bytes, u32 offset }
  bitmap rows, MSB first, offsets relative to the start of the bitmap data
"""
import argparse
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(ROOT, 'app', 'resources', 'fonts', 'BebasNeue-Regular.ttf')
OUT = os.path.join(ROOT, 'app', 'resources', 'data', 'digits.bin')

# Height of a digit in the atlas. The watch scales it down to its ring sizes.
GLYPH_HEIGHT = 120


def render():
    probe = ImageFont.truetype(FONT, 400)
    top, bottom = 10 ** 9, -10 ** 9
    for d in '0123456789':
        box = probe.getbbox(d)
        top, bottom = min(top, box[1]), max(bottom, box[3])
    size = round(400 * GLYPH_HEIGHT / (bottom - top))
    font = ImageFont.truetype(FONT, size)

    boxes = [font.getbbox(d) for d in '0123456789']
    top = min(b[1] for b in boxes)
    bottom = max(b[3] for b in boxes)
    height = bottom - top

    glyphs = []
    for d, box in zip('0123456789', boxes):
        canvas = Image.new('L', (box[2] - box[0] + 8, height), 0)
        ImageDraw.Draw(canvas).text((4 - box[0], -top), d, font=font, fill=255)
        ink = canvas.point(lambda v: 255 if v >= 128 else 0).getbbox()
        img = canvas.crop((ink[0], 0, ink[2], height))
        width = img.width
        stride = (width + 7) // 8
        rows = bytearray()
        px = img.load()
        for y in range(height):
            row = bytearray(stride)
            for x in range(width):
                if px[x, y] >= 128:
                    row[x // 8] |= 0x80 >> (x % 8)
            rows += row
        glyphs.append((width, stride, bytes(rows)))

    header = b'RD01' + struct.pack('<BBH', len(glyphs), 0, height)
    table = b''
    data = b''
    for width, stride, rows in glyphs:
        table += struct.pack('<HHI', width, stride, len(data))
        data += rows
    return header + table + data


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--check', action='store_true',
                        help='fail if the committed atlas differs from a fresh render')
    args = parser.parse_args()
    blob = render()
    if args.check:
        with open(OUT, 'rb') as f:
            if f.read() != blob:
                sys.exit('digits.bin is out of date; run tools/gen_digits.py')
        return
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, 'wb') as f:
        f.write(blob)
    print('wrote {} ({} bytes)'.format(os.path.relpath(OUT, ROOT), len(blob)))


if __name__ == '__main__':
    main()
