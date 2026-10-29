"""Bounded direct-HDR contract probe using existing native AC7 HDR and guides."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import sys
from check_hdr import uf
from check_frame import raw


def prepare(source, output):
    if output.exists():
        raise ValueError('Probe input directory must be new')
    data = (source / 'original-hdr.raw').read_bytes()
    if len(data) != 16 + 2560 * 1440 * 4 or struct.unpack_from('<4I', data) != (2560, 1440, 26, 4):
        raise ValueError('Unexpected source layout')
    color = bytearray(16 + 2560 * 1440 * 8)
    struct.pack_into('<4I', color, 0, 2560, 1440, 10, 8)
    for i, (word,) in enumerate(struct.iter_unpack('<I', memoryview(data)[16:])):
        rgb = (uf(word & 2047, 6), uf((word >> 11) & 2047, 6), uf(word >> 22, 5))
        struct.pack_into('<4e', color, 16 + i * 8, *rgb, 1)
    output.mkdir(parents=True)
    (output / 'color.raw').write_bytes(color)
    for name in ['depth.raw', 'motion.raw']:
        shutil.copyfile(source / name, output / name)
    record = {'sourceHdrSha256': hashlib.sha256(data).hexdigest(), 'colorSha256': hashlib.sha256(color).hexdigest(), 'conversion': 'R11G11B10 -> RGBA16F exact format conversion only; NO tone map, scaling, exposure or gamma', 'size': [2560, 1440]}
    (output / 'input.json').write_text(json.dumps(record, indent=2), encoding='utf8')
    print(json.dumps(record, indent=2))


def check(source, output):
    before, after = raw(source / 'color.raw'), raw(output / 'nr-output.raw')
    input_hdr, output_hdr, not_preserved, exactly_one, changed_rgb, nonfinite, differs_from_clamp = 0, 0, 0, 0, 0, 0, 0
    peak_in, peak_out, max_delta = 0., 0., 0.
    import math
    for a, b in zip(struct.iter_unpack('<4e', memoryview(before)[16:]), struct.iter_unpack('<4e', memoryview(after)[16:])):
        changed_rgb += a[:3] != b[:3]
        for x, y in zip(a[:3], b[:3]):
            if not math.isfinite(y):
                nonfinite += 1
                continue
            peak_in, peak_out = max(peak_in, x), max(peak_out, y)
            max_delta = max(max_delta, abs(x - y))
            output_hdr += y > 1
            differs_from_clamp += y != min(1, max(0, x))
            if x > 1:
                input_hdr += 1
                not_preserved += x != y
                exactly_one += y == 1
    record = {'inputMaxRgb': peak_in, 'outputMaxRgb': peak_out, 'inputChannelsAboveOne': input_hdr, 'outputChannelsAboveOne': output_hdr, 'inputHdrChannelsNotPreserved': not_preserved, 'inputHdrChannelsBecomingExactlyOne': exactly_one, 'changedRgbPixels': changed_rgb, 'nonfiniteOutputChannels': nonfinite, 'maxAbsoluteChannelChange': max_delta, 'channelsDifferentFromClampedInput': differs_from_clamp}
    (output / 'direct-hdr-checks.json').write_text(json.dumps(record, indent=2), encoding='utf8')
    print(json.dumps(record, indent=2))
    if nonfinite:
        raise ValueError('Nonfinite direct-HDR probe output')


if __name__ == '__main__':
    if len(sys.argv) != 4 or sys.argv[1] not in ['prepare', 'check']:
        raise ValueError('Usage: probe_hdr.py prepare|check SOURCE_DIRECTORY NEW_INPUT_OR_EVALUATION_DIRECTORY')
    {'prepare': prepare, 'check': check}[sys.argv[1]](Path(sys.argv[2]), Path(sys.argv[3]))
