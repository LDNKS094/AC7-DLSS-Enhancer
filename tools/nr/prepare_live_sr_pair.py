"""Prepare exactly two audited real HDR frames for same-feature SR execution, no NR stage."""
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
from check_hdr import uf
from prepare_frame import read_raw, back_motion


def audited_frames(source, audit):
    q = audit['qualification']
    count = audit['manifestUnmodified']['frames']
    if count == 8:
        accepted = q['adjacentInputSupported'] and q['dynamicInputSupported']
        frames = audit['frames']
    elif count == 2:
        accepted = q['adjacentRenderedIntervalsSupported'] and q['simulationAdvanceObserved']
        frames = audit['viewMetadataCandidates']
    else:
        raise ValueError('Only fixed 2/8 audited inputs supported')
    if not accepted or len(frames) != count:
        raise ValueError('Requires audited real advancing sequence')
    for p in audit['provenance']:
        if hashlib.sha256((source / p['name']).read_bytes()).hexdigest() != p['sha256']:
            raise ValueError('Source changed after temporal audit')
    if audit['manifestUnmodified']['allocation'] != [5120, 1440] or audit['manifestUnmodified']['rect'] != [1280, 0, 2560, 1440]:
        raise ValueError('Unsupported actual sequence layout')
    for i, frame in enumerate(frames):
        view = (source / f'frame{i}-view.bin').read_bytes()
        if len(view) != 4096 or frame['counter'] != struct.unpack_from('<I', view, 137 * 16 + 4)[0]:
            raise ValueError('Audited frame counter disagrees with source View')
        if count == 8 and (frame['frame'] != i or frame['currentTimes'] != list(struct.unpack_from('<2f', view, 136 * 16 + 8)) or
                           frame['previousTimes'] != list(struct.unpack_from('<2f', view, 133 * 16 + 8))):
            raise ValueError('Audited index/time disagrees with source View')
        if i and frame['counter'] != ((frames[i - 1]['counter'] + 1) & 0xffffffff):
            raise ValueError('Audited sequence counter discontinuity')
    return frames


def main():
    source, output = map(Path, sys.argv[1:])
    if output.exists():
        raise ValueError('Refusing existing prepared pair')
    audit = json.loads((source / 'audit.json').read_text())
    metadata = audited_frames(source, audit)
    output.mkdir(parents=True)
    frames = []
    for f in range(len(metadata)):
        scene = read_raw(source / f'frame{f}-scene.raw', (5120, 1440, 26, 4))
        depth = read_raw(source / f'frame{f}-depth.raw', (5120, 1440, 19, 8))
        motion = read_raw(source / f'frame{f}-velocity.raw', (5120, 1440, 35, 4))
        view = (source / f'frame{f}-view.bin').read_bytes()
        if len(view) != 4096 or struct.unpack_from('<4f', view, 114 * 16) != (0, 0, 0, 0):
            raise ValueError('This diagnostic supports captured zero jitter only')
        matrix = [struct.unpack_from('<4f', view, r * 16) for r in range(110, 114)]
        color = bytearray(2560 * 1440 * 8)
        dep = bytearray(2560 * 1440 * 4)
        mv = bytearray(2560 * 1440 * 4)
        max_error, max_motion = 0., 0.
        for y in range(1440):
            cy = (y - 719.5) / -720
            for x in range(2560):
                full = y * 5120 + x + 1280
                pixel = y * 2560 + x
                word = struct.unpack_from('<I', scene, full * 4)[0]
                rgb = (uf(word & 2047, 6), uf((word >> 11) & 2047, 6), uf(word >> 22, 5))
                if not all(math.isfinite(v) and 0 <= v <= 65504 for v in rgb):
                    raise ValueError('Invalid scene HDR')
                struct.pack_into('<4e', color, pixel * 8, *rgb, 1)
                if struct.unpack_from('<4e', color, pixel * 8)[:3] != rgb:
                    raise ValueError('HDR conversion not exact')
                z = struct.unpack_from('<f', depth, full * 8)[0]
                if not math.isfinite(z) or not 0 <= z <= 1:
                    raise ValueError('Invalid native depth')
                dep[pixel * 4:pixel * 4 + 4] = depth[full * 8:full * 8 + 4]
                vx, vy = struct.unpack_from('<2H', motion, full * 4)
                if vx:
                    nx, ny = vx / 65535 * 4.008016 - 2.003978, vy / 65535 * 4.008016 - 2.003978
                else:
                    cx = (x - 1279.5) / 1280
                    p = [cx * matrix[0][k] + cy * matrix[1][k] + z * matrix[2][k] + matrix[3][k] for k in (0, 1, 3)]
                    if not all(math.isfinite(v) for v in p) or abs(p[2]) < 1e-12:
                        raise ValueError('Invalid camera reprojection')
                    nx, ny = cx - p[0] / p[2], cy - p[1] / p[2]
                # Match the existing GPU normalizer's float32 NDC output before FP16 pixel conversion.
                nx, ny = struct.unpack('<2f', struct.pack('<2f', nx, ny))
                px, py = back_motion(nx, ny, 2560, 1440)
                if not all(math.isfinite(v) and abs(v) <= 65504 for v in (px, py)):
                    raise ValueError('Motion cannot be represented in FP16')
                struct.pack_into('<2e', mv, pixel * 4, px, py)
                hx, hy = struct.unpack_from('<2e', mv, pixel * 4)
                max_error = max(max_error, abs(hx - px), abs(hy - py))
                max_motion = max(max_motion, math.hypot(px, py))
        gain = struct.unpack('<2f', read_raw(source / f'frame{f}-exposure.raw', (1, 1, 16, 8)))[0]
        if not math.isfinite(gain) or gain <= 0:
            raise ValueError('Invalid native exposure')
        folder = output / f'frame{f}'
        folder.mkdir()
        files = [('color.raw', (2560, 1440, 10, 8), color), ('depth.raw', (2560, 1440, 41, 4), dep),
                 ('motion.raw', (2560, 1440, 34, 4), mv), ('exposure.raw', (1, 1, 41, 4), struct.pack('<f', gain))]
        hashes = {}
        for name, header, payload in files:
            data = struct.pack('<4I', *header) + payload
            (folder / name).write_bytes(data)
            hashes[name] = hashlib.sha256(data).hexdigest()
        frames.append({'index': f, 'sourceFrameCandidate': metadata[f]['counter'], 'hashes': hashes,
                       'maxMotionPixels': max_motion, 'maxHalfConversionComponentError': max_error, 'exposureGain': gain})
        print(f'Prepared actual frame {f}; max motion {max_motion:.6g}px; half error {max_error:.6g}', flush=True)
    record = {'kind': 'REAL_DYNAMIC_SEQUENCE_SR_HDR_DIAGNOSTIC', 'source': str(source), 'frames': frames,
              'preExposure': 1, 'exposureScale': 1, 'jitter': [0, 0], 'size': [2560, 1440],
              'color': 'Original native prewarp HDR, exact R11 to RGBA16F; no NR applied in this SR history isolation test',
              'motion': 'CPU shader-formula reference, float32 NDC then backward pixel FP16; actual View/rect/depth, no old guides reused',
              'limits': 'Fixed 2/8-frame API/history execution only; P1 uncalibrated, no actual jitter, transparent/disocclusion guide semantics incomplete, no realtime/game writeback or quality acceptance'}
    (output / 'sequence-contract.json').write_text(json.dumps(record, indent=2), encoding='utf8')


if __name__ == '__main__':
    assert back_motion(.5, -.25, 100, 80) == (-25, -10)
    assert uf(15 << 6, 6) == 1
    if len(sys.argv) != 3:
        raise ValueError('Usage: prepare_live_sr_pair.py AUDITED_LIVE_PAIR NEW_OUTPUT')
    main()
