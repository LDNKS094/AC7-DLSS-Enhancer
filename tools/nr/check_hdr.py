"""Independently inspect the HDR restoration readback and make one display preview."""
import json
import math
from pathlib import Path
import struct
import sys
from check_frame import png, raw


def uf(value, mantissa_bits):
    exponent = value >> mantissa_bits
    mantissa = value & ((1 << mantissa_bits) - 1)
    if exponent == 31:
        return math.nan if mantissa else math.inf
    return math.ldexp(mantissa, -14 - mantissa_bits) if exponent == 0 else math.ldexp(1 + mantissa / (1 << mantissa_bits), exponent - 15)


def aces(value):
    return min(1, max(0, (value * (2.51 * value + .03)) / (value * (2.43 * value + .59) + .14)))


def main():
    if len(sys.argv) != 3:
        raise ValueError('Usage: check_hdr.py PREPARED_HDR_DIRECTORY EVALUATION_DIRECTORY')
    source, output = map(Path, sys.argv[1:])
    if (output / 'hdr-preview.png').exists():
        raise ValueError('Refusing to overwrite HDR preview evidence')
    packed = (source / 'original-hdr.raw').read_bytes()
    if len(packed) != 16 + 2560 * 1440 * 4 or struct.unpack_from('<4I', packed) != (2560, 1440, 26, 4):
        raise ValueError('Unexpected original HDR layout')
    exp = (source / 'exposure.raw').read_bytes()
    if len(exp) != 24 or struct.unpack_from('<4I', exp) != (1, 1, 16, 8):
        raise ValueError('Unexpected exposure layout')
    exposure = struct.unpack_from('<f', exp, 16)[0]
    if not math.isfinite(exposure) or exposure <= 0:
        raise ValueError('Invalid exposure')
    restored = raw(output / 'hdr-restored.raw')
    pixels = bytearray(2560 * 1440 * 3)
    finite, above_one, invalid, double_gain, tenfold_gain, protected, protected_changed = 0, 0, 0, 0, 0, 0, 0
    max_value, max_relative, worst = 0., 0., None
    for i, ((word,), after) in enumerate(zip(struct.iter_unpack('<I', memoryview(packed)[16:]), struct.iter_unpack('<4e', memoryview(restored)[16:]))):
        before = (uf(word & 2047, 6), uf((word >> 11) & 2047, 6), uf(word >> 22, 5))
        if not all(math.isfinite(v) and 0 <= v <= 65504 for v in after):
            invalid += 1
            continue
        if max(before) * exposure >= 8:
            protected += 1
            protected_changed += before != after[:3]
        for c in range(3):
            a, b = before[c], after[c]
            finite += 1
            above_one += b > 1
            max_value = max(max_value, b)
            if a > 0:
                double_gain += b > a * 2
                tenfold_gain += b > a * 10
                relative = abs(b - a) / a
                if relative > max_relative:
                    max_relative, worst = relative, {'pixel': [i % 2560, i // 2560], 'channel': c, 'beforeRgb': before, 'afterRgb': after[:3], 'absoluteChannelChange': abs(b - a)}
            pixels[i * 3 + c] = round(aces(b * exposure) ** (1 / 2.2) * 255)
    record = {
        'exposure': exposure, 'finiteRgbChannels': finite, 'invalidPixels': invalid, 'channelsAboveOne': above_one, 'maxRestoredRgb': max_value,
        'fullyProtectedPixels': protected, 'fullyProtectedChangedPixels': protected_changed,
        'channelsGainingMoreThan2x': double_gain, 'channelsGainingMoreThan10x': tenfold_gain,
        'maxRelativeChannelChange': max_relative, 'worstRelativeChange': worst,
        'limits': 'HDR prepare/reconstruction are CPU references; only NR ran on GPU. Ratio changes are not ground-truth errors. Preview reapplies this test ACES/gamma curve, not AC7 final tonemap/LUT/bloom. No temporal, performance or game reintegration claim.'
    }
    (output / 'hdr-readback-checks.json').write_text(json.dumps(record, indent=2), encoding='utf8')
    print(json.dumps(record, indent=2))
    if invalid or finite != 2560 * 1440 * 3 or protected_changed:
        raise ValueError('Invalid HDR output or fully protected highlights changed')
    png(output / 'hdr-preview.png', pixels)


if __name__ == '__main__':
    assert uf(15 << 6, 6) == 1 and uf(1, 6) == 2 ** -20 and aces(0) == 0
    main()
