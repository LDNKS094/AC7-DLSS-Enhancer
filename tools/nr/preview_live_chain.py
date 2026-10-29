"""Diagnostic ACES/gamma contact sheet, not AC7's original postprocess/LUT or temporal quality."""
from pathlib import Path
import struct
import json
import sys
import zlib
from check_hdr import aces

root, output = map(Path, sys.argv[1:])
if output.exists():
    raise ValueError('Refusing existing preview')
frames = json.loads((root / 'sr-input/sequence-contract.json').read_text())['frames']
if len(frames) not in (2, 8):
    raise ValueError('Only fixed2/8 previews supported')
width, height = 1920, 360 * len(frames)
image = bytearray(width * height * 3)
reference = root / 'cpu-reference' if (root / 'cpu-reference').exists() else root.parent / 'sr-live-history/input'
for frame in range(len(frames)):
    paths = [reference / f'frame{frame}/color.raw',
             root / 'sr-input' / f'frame{frame}/color.raw', root / 'history' / f'frame{frame}/sr-output.raw']
    for panel, path in enumerate(paths):
        raw = path.read_bytes()
        if len(raw) != 16 + 2560 * 1440 * 8 or struct.unpack_from('<4I', raw) != (2560, 1440, 10, 8):
            raise ValueError('Unexpected image layout')
        for y in range(360):
            for x in range(640):
                rgb = struct.unpack_from('<3e', raw, 16 + ((y * 4 + 2) * 2560 + x * 4 + 2) * 8)
                at = ((frame * 360 + y) * width + panel * 640 + x) * 3
                for c, value in enumerate(rgb):
                    image[at + c] = round(aces(max(0, value)) ** (1 / 2.2) * 255)
def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
rows = b''.join(b'\0' + image[y * width * 3:(y + 1) * width * 3] for y in range(height))
output.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', width, height, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))
print('Rows: actual frames ' + '/'.join(str(f['sourceFrameCandidate']) for f in frames) + '. Columns: original HDR / NR restored HDR / NR+SR history. Diagnostic ACES/gamma only.')
