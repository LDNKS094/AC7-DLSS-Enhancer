"""Check one reset-frame NR readback and save SDR previews; no model re-execution."""
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import zlib


def raw(file):
    data = file.read_bytes()
    if len(data) < 16 or struct.unpack_from('<4I', data) != (2560, 1440, 10, 8) or len(data) != 16 + 2560 * 1440 * 8:
        raise ValueError(f'Unexpected RGBA16F layout: {file}')
    return data


def png(file, pixels):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + pixels[y * 2560 * 3:(y + 1) * 2560 * 3] for y in range(1440))
    file.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', 2560, 1440, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def main():
    if len(sys.argv) != 3:
        raise ValueError('Usage: check_frame.py PREPARED_FRAME_DIRECTORY EVALUATION_DIRECTORY')
    source, output = map(Path, sys.argv[1:])
    src, dst = raw(source / 'color.raw'), raw(output / 'nr-output.raw')
    previews = [bytearray(2560 * 1440 * 3), bytearray(2560 * 1440 * 3)]
    finite, invalid, negatives, above_one, changed, alpha_invalid, sentinel_rgb = 0, 0, 0, 0, 0, 0, 0
    lo, hi, absolute, square, max_error = math.inf, -math.inf, 0., 0., 0.
    for i, (before, after) in enumerate(zip(struct.iter_unpack('<4e', memoryview(src)[16:]), struct.iter_unpack('<4e', memoryview(dst)[16:]))):
        if before[:3] != after[:3]:
            changed += 1
        if not math.isfinite(after[3]):
            alpha_invalid += 1
        for c in range(3):
            a, b = before[c], after[c]
            if not math.isfinite(a):
                raise ValueError('Input is not finite')
            previews[0][i * 3 + c] = round(min(1, max(0, a)) * 255)
            if not math.isfinite(b):
                invalid += 1
                if struct.unpack_from('<H', dst, 16 + i * 8 + c * 2)[0] == 0x7e00:
                    sentinel_rgb += 1
                continue
            finite += 1
            lo, hi = min(lo, b), max(hi, b)
            negatives += b < 0
            above_one += b > 1
            e = abs(a - b)
            absolute += e
            square += e * e
            max_error = max(max_error, e)
            previews[1][i * 3 + c] = round(min(1, max(0, b)) * 255)
    record = {
        'size': [2560, 1440], 'inputSha256': hashlib.sha256(src).hexdigest(), 'outputSha256': hashlib.sha256(dst).hexdigest(),
        'finiteOutputRgbChannels': finite, 'nonfiniteOutputRgbChannels': invalid, 'nanSentinelRgbChannels': sentinel_rgb,
        'negativeOutputRgbChannels': negatives, 'aboveOneOutputRgbChannels': above_one,
        'outputMinRgb': lo if finite else None, 'outputMaxRgb': hi if finite else None,
        'changedRgbPixels': changed, 'rgbPixelCount': 2560 * 1440,
        'meanAbsoluteDifferenceSdrCode': absolute / finite if finite else None,
        'rootMeanSquareDifferenceSdrCode': math.sqrt(square / finite) if finite else None,
        'maxAbsoluteDifferenceSdrCode': max_error, 'nonfiniteAlphaPixels': alpha_invalid,
        'limits': 'Difference is not an error against ground truth or a quality score. SDR previews clamp to [0,1] for display; raw data and statistics do not clamp. Single reset frame does not validate temporal motion, HDR reintegration, Pre-SR, performance or AC7 live hooks.'
    }
    (output / 'checks.json').write_text(json.dumps(record, indent=2), encoding='utf8')
    print(json.dumps(record, indent=2))
    if invalid or not finite or not changed:
        raise ValueError('NR output has invalid/unwritten RGB or is an identity result')
    png(output / 'input-preview.png', previews[0])
    png(output / 'nr-preview.png', previews[1])


if __name__ == '__main__':
    main()
