"""Pair fresh D3D11 GPU proxy/guides with the audited live pair; no stale guide reuse."""
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
from prepare_frame import read_raw, back_motion
from prepare_live_sr_pair import audited_frames


def half_reference_tolerance(value):
    # Compare an unrounded GPU value with a CPU half: allow its half-bin plus float32 arithmetic error.
    exponent = math.frexp(abs(value))[1] - 11 if value else -24
    return 0.5 * 2 ** max(-24, exponent) + .001


def main():
    source, gpu, reference, output = map(Path, sys.argv[1:])
    if output.exists():
        raise ValueError('Refusing existing NR pair input')
    audit = json.loads((source / 'audit.json').read_text())
    metadata = audited_frames(source, audit)
    if not (gpu / 'complete.txt').exists():
        raise ValueError('GPU preparation did not complete')
    for p in audit['provenance']:
        if hashlib.sha256((source / p['name']).read_bytes()).hexdigest() != p['sha256']:
            raise ValueError('Live source identity changed')
    output.mkdir(parents=True)
    records = []
    for f in range(len(metadata)):
        folder = gpu / f'frame{f}'
        proxy = read_raw(folder / 'proxy.raw', (2560, 1440, 10, 8))
        for rgba in struct.iter_unpack('<4e', proxy):
            if not all(math.isfinite(v) and 0 <= v <= 1 for v in rgba[:3]):
                raise ValueError('GPU proxy is not finite SDR')
        if (folder / 'scene-restored.raw').read_bytes() != (source / f'frame{f}-scene.raw').read_bytes():
            raise ValueError('GPU neutral source mismatch')
        depth = read_raw(folder / 'depth.raw', (2560, 1440, 41, 4))
        if (folder / 'depth.raw').read_bytes() != (reference / f'frame{f}' / 'depth.raw').read_bytes():
            raise ValueError('GPU normalized depth not exact to native CPU crop')
        mask = read_raw(folder / 'validity.raw', (2560, 1440, 42, 4))
        if any(v[0] not in (1, 2) for v in struct.iter_unpack('<I', mask)):
            raise ValueError('Invalid GPU guide pixels')
        ndc = read_raw(folder / 'motion-ndc.raw', (2560, 1440, 16, 8))
        cpu = read_raw(reference / f'frame{f}' / 'motion.raw', (2560, 1440, 34, 4))
        mv = bytearray(2560 * 1440 * 4)
        error, half_error = 0., 0.
        for i, (x, y) in enumerate(struct.iter_unpack('<2f', ndc)):
            px, py = back_motion(x, y, 2560, 1440)
            if not all(math.isfinite(v) and abs(v) <= 65504 for v in (px, py)):
                raise ValueError('Invalid GPU normalized motion')
            struct.pack_into('<2e', mv, i * 4, px, py)
            a, b = struct.unpack_from('<2e', mv, i * 4)
            cx, cy = struct.unpack_from('<2e', cpu, i * 4)
            error = max(error, abs(a - cx), abs(b - cy))
            half_error = max(half_error, abs(a - px), abs(b - py))
            if abs(px - cx) > half_reference_tolerance(cx) or abs(py - cy) > half_reference_tolerance(cy):
                raise ValueError('GPU float motion exceeds CPU half quantization bin plus .001 pixel arithmetic tolerance')
        dest = output / f'frame{f}'
        dest.mkdir()
        files = {'color.raw': struct.pack('<4I', 2560, 1440, 10, 8) + proxy,
                 'depth.raw': struct.pack('<4I', 2560, 1440, 41, 4) + depth,
                 'motion.raw': struct.pack('<4I', 2560, 1440, 34, 4) + mv}
        for name, data in files.items():
            (dest / name).write_bytes(data)
        records.append({'frame': f, 'maxGpuVsCpuHalfPixelError': error, 'maxGpuHalfConversionError': half_error,
                        'hashes': {n: hashlib.sha256(b).hexdigest() for n, b in files.items()}})
    record = {'kind': 'REAL_LIVE_PAIR_GPU_PROXY_NR_INPUT', 'source': str(source), 'gpu': str(gpu), 'frames': records,
              'resetSequence': [1] + [0] * (len(metadata) - 1), 'jitter': [0, 0], 'limits': 'GPU conversion and API history contract only; full guide/transparent/occlusion quality not accepted'}
    (output / 'sequence-contract.json').write_text(json.dumps(record, indent=2), encoding='utf8')
    print(json.dumps(record, indent=2))


if __name__ == '__main__':
    assert half_reference_tolerance(64) == .03225
    assert abs(64.02 - 64) < half_reference_tolerance(64)
    assert abs(64.1 - 64) > half_reference_tolerance(64)
    assert half_reference_tolerance(0) < .001001
    if len(sys.argv) != 5:
        raise ValueError('Usage: prepare_live_nr_pair.py AUDITED_LIVE_PAIR GPU_PAIR SR_CPU_REFERENCE NEW_NR_PAIR')
    main()
