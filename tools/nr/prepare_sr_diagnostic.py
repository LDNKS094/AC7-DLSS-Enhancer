"""Explicit one-reset-frame SR diagnostic; these assumptions do not calibrate AC7."""
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import sys
from check_hdr import uf


def main():
    if len(sys.argv) != 5:
        raise ValueError('Usage: prepare_sr_diagnostic.py FULL_SCENE_HDR_RAW VERIFIED_GUIDES_DIR CAPTURE_EXPOSURE_RAW NEW_DIRECTORY')
    scene, guides, exposure, output = map(Path, sys.argv[1:])
    if output.exists():
        raise ValueError('Diagnostic directory must be new')
    data = scene.read_bytes()
    if len(data) != 16 + 5120 * 1440 * 4 or struct.unpack_from('<4I', data) != (5120, 1440, 26, 4):
        raise ValueError('Unexpected full scene HDR layout')
    exp = exposure.read_bytes()
    if len(exp) != 24 or struct.unpack_from('<4I', exp) != (1, 1, 16, 8):
        raise ValueError('Unexpected captured exposure layout')
    gain = struct.unpack_from('<f', exp, 16)[0]
    if not math.isfinite(gain) or gain <= 0:
        raise ValueError('Invalid captured gain')
    color = bytearray(16 + 2560 * 1440 * 8)
    struct.pack_into('<4I', color, 0, 2560, 1440, 10, 8)
    peak, above_one = 0., 0
    for y in range(1440):
        for x in range(2560):
            word = struct.unpack_from('<I', data, 16 + (y * 5120 + x + 1280) * 4)[0]
            rgb = (uf(word & 2047, 6), uf((word >> 11) & 2047, 6), uf(word >> 22, 5))
            if not all(math.isfinite(v) and 0 <= v <= 65504 for v in rgb):
                raise ValueError('Invalid HDR input')
            at = 16 + (y * 2560 + x) * 8
            struct.pack_into('<4e', color, at, *rgb, 1)
            if struct.unpack_from('<4e', color, at)[:3] != rgb:
                raise ValueError('Input format conversion was not exact')
            peak, above_one = max(peak, *rgb), above_one + sum(v > 1 for v in rgb)
    output.mkdir(parents=True)
    (output / 'color.raw').write_bytes(color)
    for name, expected in [('depth.raw', (2560, 1440, 41, 4)), ('motion.raw', (2560, 1440, 34, 4))]:
        raw = (guides / name).read_bytes()
        if struct.unpack_from('<4I', raw) != expected or len(raw) != 16 + 2560 * 1440 * expected[3]:
            raise ValueError('Unexpected verified guide layout')
        shutil.copyfile(guides / name, output / name)
    (output / 'exposure.raw').write_bytes(struct.pack('<4If', 1, 1, 41, 4, gain))
    record = {
        'kind': 'EXPLICIT_STANDALONE_SR_RESET_DIAGNOSTIC',
        'sourceScene': str(scene), 'sourceSha256': hashlib.sha256(data).hexdigest(),
        'inputColorSha256': hashlib.sha256(color).hexdigest(), 'size': [2560, 1440],
        'formatConversionExact': True, 'inputMaxRgb': peak, 'inputChannelsAboveOne': above_one,
        'reset': 1, 'jitter': [0, 0], 'jitterBasis': 'captured frame actual view CB row114',
        'motion': 'same capture backward pixel displacement; independent temporal calibration not complete',
        'depthInverted': True, 'exposureTextureGain': gain,
        'preExposure': 1, 'exposureScale': 1,
        'assumption': 'Treat the provided numeric HDR buffer as a standalone diagnostic domain with no additional pre-exposure. This does NOT assert the game pre-exposure is1 or that its HDR/color/exposure semantics are calibrated.',
        'limits': 'Single reset, same-size DLAA only. No temporal acceptance, upscaling, game writeback, quality/performance or real-time bridge claim.'
    }
    (output / 'diagnostic-contract.json').write_text(json.dumps(record, indent=2), encoding='utf8')
    print(json.dumps(record, indent=2))


if __name__ == '__main__':
    assert uf(15 << 6, 6) == 1 and struct.unpack('<e', struct.pack('<e', 166))[0] == 166
    main()
