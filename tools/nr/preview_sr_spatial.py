"""Nearest-neighbor local comparisons from existing HDR raw; no model or game calls."""
from pathlib import Path
from PIL import Image, ImageDraw
import json
import struct
import sys


def preview(data, box, ev):
    x0, y0, x1, y1 = box
    rgb = bytearray((x1 - x0) * (y1 - y0) * 3)
    for y in range(y0, y1):
        for x in range(x0, x1):
            values = struct.unpack_from('<4e', data, 16 + (y * 2560 + x) * 8)
            for c in range(3):
                v = max(0, values[c] * 2 ** ev)
                v = min(1, (v * (2.51 * v + .03)) / (v * (2.43 * v + .59) + .14))
                rgb[((y - y0) * (x1 - x0) + x - x0) * 3 + c] = round(v ** (1 / 2.2) * 255)
    return Image.frombytes('RGB', (x1 - x0, y1 - y0), bytes(rgb)).resize((512, 512), Image.Resampling.NEAREST)


def main():
    root, output = map(Path, sys.argv[1:])
    if output.exists():
        raise ValueError('Refusing to overwrite comparison')
    specs = [('baseline', [504, 1033, 568, 1097], -6, 'Worst delta: sea highlight, working rect (536,1065)'),
             ('baseline', [1160, 880, 1288, 1008], 0, 'Aircraft detail: illustrative crop'),
             ('control', [96, 1048, 160, 1112], -6, 'Synthetic160 single pixel'),
             ('control', [1632, 1048, 1696, 1112], -6, 'Synthetic160 large square interior')]
    canvas = Image.new('RGB', (1024, len(specs) * 538), (24, 24, 24))
    draw = ImageDraw.Draw(canvas)
    for row, (name, box, ev, label) in enumerate(specs):
        prefix = 'reset-spatial-control' if name == 'control' else 'reset-diagnostic-' + name
        source = (root / (prefix + '-input') / 'color.raw').read_bytes()
        target = (root / (prefix + '-output') / 'sr-output.raw').read_bytes()
        for col, data in enumerate([source, target]):
            if len(data) != 16 + 2560 * 1440 * 8 or struct.unpack_from('<4I', data) != (2560, 1440, 10, 8):
                raise ValueError('Unexpected raw layout')
            canvas.paste(preview(data, box, ev), (col * 512, row * 538 + 26))
            draw.text((col * 512 + 8, row * 538 + 7), ('INPUT ' if col == 0 else 'SR RESET ') + label + f' EV{ev}', fill='white')
    output.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(output)
    output.with_suffix('.json').write_text(json.dumps({'source': str(root), 'crops': specs, 'sampling': 'nearest', 'curve': 'diagnostic ACES/gamma2.2 with stated EV; not AC7 LUT', 'limit': 'Local appearance only, no temporal or quality verdict'}, indent=2), encoding='utf8')
    print(output)


if __name__ == '__main__':
    assert len(sys.argv) == 3, 'Usage: preview_sr_spatial.py SR_ARTIFACT_ROOT NEW_PNG'
    main()
