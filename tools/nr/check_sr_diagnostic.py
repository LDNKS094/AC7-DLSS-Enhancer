"""Inspect HDR SR Reset diagnostic outputs without treating assumptions as calibration."""
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
from check_frame import raw, png
from check_hdr import aces


def main():
    if len(sys.argv) != 3:
        raise ValueError('Usage: check_sr_diagnostic.py INPUT_DIRECTORY OUTPUT_DIRECTORY')
    source, output = map(Path, sys.argv[1:])
    contract = json.loads((source / 'diagnostic-contract.json').read_text(encoding='utf8'))
    if contract.get('kind') != 'EXPLICIT_STANDALONE_SR_RESET_DIAGNOSTIC' or contract.get('reset') != 1:
        raise ValueError('Missing explicit diagnostic contract')
    original, result = raw(source / 'color.raw'), raw(output / 'sr-output.raw')
    if hashlib.sha256(original).hexdigest() != contract['inputColorSha256']:
        raise ValueError('Input identity mismatch')
    finite, invalid, above_one, negative, changed, invalid_alpha = 0, 0, 0, 0, 0, 0
    minimum, maximum, absolute, peak = math.inf, -math.inf, 0., 0.
    display = bytearray(2560 * 1440 * 3)
    for i, (a, b) in enumerate(zip(struct.iter_unpack('<4e', memoryview(original)[16:]), struct.iter_unpack('<4e', memoryview(result)[16:]))):
        changed += a[:3] != b[:3]
        invalid_alpha += not math.isfinite(b[3])
        for c in range(3):
            x, y = a[c], b[c]
            if not math.isfinite(y):
                invalid += 1
                continue
            finite += 1
            minimum, maximum = min(minimum, y), max(maximum, y)
            above_one += y > 1
            negative += y < 0
            error = abs(y - x)
            absolute += error
            peak = max(peak, error)
            display[i * 3 + c] = round(aces(max(0, y) * contract['exposureTextureGain']) ** (1 / 2.2) * 255)
    record = {
        'kind': contract['kind'], 'size': [2560, 1440], 'reset': 1,
        'preExposureAssumption': contract['preExposure'], 'exposureScaleAssumption': contract['exposureScale'],
        'inputSha256': hashlib.sha256(original).hexdigest(), 'outputSha256': hashlib.sha256(result).hexdigest(),
        'finiteRgbChannels': finite, 'nonfiniteRgbChannels': invalid, 'negativeRgbChannels': negative,
        'channelsAboveOne': above_one, 'minRgb': minimum if finite else None, 'maxRgb': maximum if finite else None,
        'nonfiniteAlphaPixels': invalid_alpha, 'changedRgbPixels': changed,
        'meanAbsoluteHdrDifference': absolute / finite if finite else None, 'maxAbsoluteHdrDifference': peak,
        'limits': 'API/format/finite HDR result under explicit standalone PreExposure1; not proof of AC7 exposure semantics, temporal quality, upscaling or game writeback. Differences are not reference errors. Preview uses diagnostic ACES/gamma, not game LUT.'
    }
    (output / 'checks.json').write_text(json.dumps(record, indent=2), encoding='utf8')
    print(json.dumps(record, indent=2))
    if invalid or finite != 2560 * 1440 * 3 or above_one == 0:
        raise ValueError('Invalid/unwritten RGB or HDR range not retained')
    png(output / 'sr-preview.png', display)


if __name__ == '__main__':
    main()
