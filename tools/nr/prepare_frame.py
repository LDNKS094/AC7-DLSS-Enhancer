"""Prepare the existing normal-flight capture for one reset-frame NR API test."""
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import tempfile


def read_raw(file, expected):
    data = file.read_bytes()
    if len(data) < 16 or struct.unpack_from('<4I', data) != expected:
        raise ValueError(f'Unexpected raw layout: {file}')
    w, h, _, bpp = expected
    if len(data) != 16 + w * h * bpp:
        raise ValueError(f'Truncated or excess raw payload: {file}')
    return data[16:]


def back_motion(x, y, w, h):
    # Captured guides are current-minus-previous NDC; the test uses backward screen pixels.
    return -x * w / 2, y * h / 2


def self_test():
    assert back_motion(0.5, -0.25, 100, 80) == (-25, -10)
    assert struct.unpack('<e', struct.pack('<e', 1.0))[0] == 1.0
    assert struct.pack('<4e', 1, 0, 0, 1).hex() == '003c00000000003c'
    with tempfile.TemporaryDirectory() as directory:
        file = Path(directory) / 'test.raw'
        header, payload = struct.pack('<4I', 1, 1, 41, 4), struct.pack('<f', 0.5)
        file.write_bytes(header + payload)
        assert read_raw(file, (1, 1, 41, 4)) == payload
        for invalid in [b'', header, header + payload + b'\0', struct.pack('<4I', 1, 1, 16, 4) + payload]:
            file.write_bytes(invalid)
            try:
                read_raw(file, (1, 1, 41, 4))
            except ValueError:
                pass
            else:
                raise AssertionError('Corrupt raw input was accepted')


def main():
    self_test()
    if sys.argv[1:] == ['--self-test']:
        print('PASS half packing, NDC conversion, raw roundtrip and corrupt-input rejection')
        return
    if len(sys.argv) != 3:
        raise ValueError('Usage: prepare_frame.py CAPTURE_ARTIFACT_DIRECTORY NEW_OUTPUT_DIRECTORY')
    source, output = map(Path, sys.argv[1:])
    if output.exists():
        raise ValueError('Output directory must be new; prior evidence is not overwritten')
    w, h = 2560, 1440
    paths = [source / 'color/post-tonemap.raw', source / 'checkpoints/gpu-depth-cropped.raw', source / 'checkpoints/gpu-motion-cropped.raw']
    color = read_raw(paths[0], (5120, h, 90, 4))
    depth = read_raw(paths[1], (w, h, 41, 4))
    motion = read_raw(paths[2], (w, h, 16, 8))
    if not all(math.isfinite(v[0]) and 0 <= v[0] <= 1 for v in struct.iter_unpack('<f', depth)):
        raise ValueError('Depth is nonfinite or outside hardware [0,1] range')
    rgba = bytearray(w * h * 8)
    rg = bytearray(w * h * 4)
    max_motion_error, max_motion = 0., 0.
    for y in range(h):
        for x in range(w):
            i = y * w + x
            b, g, r, _ = struct.unpack_from('<4B', color, (y * 5120 + x + 1280) * 4)
            struct.pack_into('<4e', rgba, i * 8, r / 255, g / 255, b / 255, 1)
            mx, my = back_motion(*struct.unpack_from('<2f', motion, i * 8), w, h)
            if not math.isfinite(mx) or not math.isfinite(my):
                raise ValueError('Nonfinite motion guide')
            struct.pack_into('<2e', rg, i * 4, mx, my)
            hx, hy = struct.unpack_from('<2e', rg, i * 4)
            max_motion_error = max(max_motion_error, abs(hx - mx), abs(hy - my))
            max_motion = max(max_motion, abs(mx), abs(my))
    output.mkdir(parents=True)
    for name, fmt, bpp, payload in [('color', 10, 8, rgba), ('depth', 41, 4, depth), ('motion', 34, 4, rg)]:
        (output / (name + '.raw')).write_bytes(struct.pack('<4I', w, h, fmt, bpp) + payload)
    record = {
        'capture': 'Ace7Game__2026_10_28__02_30_38', 'sourceFiles': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
        'size': [w, h], 'activeRect': [1280, 0, w, h], 'color': 'actual HUDless post-tonemap BGRA8 -> RGBA16F, no added tone map or guessed exposure',
        'depth': 'bit-exact normalized R32 hardware depth', 'motion': 'actual normalized current-minus-previous NDC -> backward pixel displacement, RG16F',
        'maxMotionPixelMagnitude': max_motion, 'maxMotionHalfQuantizationErrorPixels': max_motion_error,
        'reset': 1, 'depthInverted': 1, 'jitter': [0, 0],
        'limits': 'Reset-frame API/output validation only. Infinite reverse-Z and backward motion are explicit test contracts, not independent temporal calibration; SDR sample is not the final Pre-SR insertion. No history frames synthesized.'
    }
    (output / 'input.json').write_text(json.dumps(record, indent=2), encoding='utf8')
    print(json.dumps(record, indent=2))


if __name__ == '__main__':
    main()
