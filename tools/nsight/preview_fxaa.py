"""Decode final10bit color and show matched FXAA crops. No model/game calls."""
from pathlib import Path
from PIL import Image, ImageDraw
import struct
import sys


def crop(raw, box, width, height, fmt=24):
    x0, y0, x1, y1 = box
    rgb = bytearray(width * height * 3)
    for y in range(height):
        py = y0 + int(y * (y1 - y0) / height)
        for x in range(width):
            px = 1280 + x0 + int(x * (x1 - x0) / width)
            word = struct.unpack_from('<I', raw, 16 + (py * 5120 + px) * 4)[0]
            for c in range(3):
                rgb[(y * width + x) * 3 + c] = round(((word >> (c * 10)) & 1023) * 255 / 1023) if fmt == 24 else (word >> (16 - c * 8)) & 255
    return Image.frombytes('RGB', (width, height), bytes(rgb))


def main():
    root, output = map(Path, sys.argv[1:])
    if output.exists():
        raise ValueError('Refusing to overwrite preview')
    canvas = Image.new('RGB', (1536, 1534), (24, 24, 24))
    draw = ImageDraw.Draw(canvas)
    for col, (directory, label) in enumerate([('fxaa-retained', 'SR + retained FXAA'), ('fxaa-bypass-v2', 'SR + FXAA RGB bypass, alpha/padding preserved')]):
        raw = (root / directory / 'final.raw').read_bytes()
        if len(raw) != 16 + 5120 * 1440 * 4 or struct.unpack_from('<4I', raw) != (5120, 1440, 24, 4):
            raise ValueError('Unexpected final layout')
        before_ui = (root / directory / 'post-effects.raw').read_bytes()
        if struct.unpack_from('<4I', before_ui) != (5120, 1440, 90, 4):
            raise ValueError('Unexpected pre-UI layout')
        for box, w, h, top, region in [([0, 0, 2560, 1440], 768, 432, 0, 'Active frame'), ([1160, 880, 1288, 1008], 512, 512, 458, 'Aircraft tail before UI, nearest4x'), ([480, 1000, 608, 1128], 512, 512, 996, 'Sea highlights before UI, nearest4x')]:
            draw.text((col * 768 + 8, top + 7), label + ' / ' + region, fill='white')
            canvas.paste(crop(raw if top == 0 else before_ui, box, w, h, 24 if top == 0 else 90), (col * 768, top + 26))
    canvas.save(output)
    print(output)


if __name__ == '__main__':
    assert len(sys.argv) == 3, 'Usage: preview_fxaa.py OFFICIAL_SR_DIR NEW_PNG'
    main()
