"""Prepare new prewarp GPU-proxy input with checked provenance of existing normalized guides."""
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import sys
from prepare_frame import read_raw, back_motion


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    if len(sys.argv) != 5:
        raise ValueError('Usage: prepare_prewarp_nr.py PREWARP_REPLAY CAPTURE_ARTIFACT VERIFIED_GUIDES NEW_OUTPUT')
    replay, capture, guides, output = map(Path, sys.argv[1:])
    if output.exists():
        raise ValueError('Refusing to overwrite new NR input')
    color = read_raw(replay / 'gpu-proxy.raw', (2560, 1440, 10, 8))
    for rgba in struct.iter_unpack('<4e', color):
        if not all(math.isfinite(v) and 0 <= v <= 1 for v in rgba[:3]):
            raise ValueError('Invalid GPU SDR proxy')
    ds = read_raw(replay / 'depth-prewarp.raw', (5120, 1440, 19, 8))
    old_ds = read_raw(capture / 'checkpoints/depth.raw', (5120, 1440, 19, 8))
    # Stencil differs by stage; the normalized R32 guide uses only the first32bits.
    for i in range(0, len(ds), 8):
        if ds[i:i + 4] != old_ds[i:i + 4]:
            raise ValueError('Prewarp depth differs from guide normalization source')
    if (replay / 'motion-prewarp.raw').read_bytes() != (capture / 'checkpoints/velocity.raw').read_bytes():
        raise ValueError('Prewarp velocity differs from guide normalization source')
    depth = read_raw(guides / 'depth.raw', (2560, 1440, 41, 4))
    expected_depth = read_raw(capture / 'checkpoints/gpu-depth-cropped.raw', (2560, 1440, 41, 4))
    if depth != expected_depth:
        raise ValueError('Normalized depth guide provenance mismatch')
    ndc = read_raw(capture / 'checkpoints/gpu-motion-cropped.raw', (2560, 1440, 16, 8))
    motion = read_raw(guides / 'motion.raw', (2560, 1440, 34, 4))
    for i, value in enumerate(struct.iter_unpack('<2f', ndc)):
        if struct.pack('<2e', *back_motion(*value, 2560, 1440)) != motion[i * 4:i * 4 + 4]:
            raise ValueError('Backward pixel velocity guide provenance mismatch')
    scene = replay / 'scene-input.raw'
    if scene.read_bytes() != (replay / 'scene-before-distortion.raw').read_bytes():
        raise ValueError('Unexpected source scene identity')
    output.mkdir(parents=True)
    shutil.copyfile(replay / 'gpu-proxy.raw', output / 'color.raw')
    for name in ['depth.raw', 'motion.raw']:
        shutil.copyfile(guides / name, output / name)
    record = {
        'kind': 'PREWARP_GPU_PROXY_NR_RESET_INPUT', 'boundary': 'after465 before466',
        'sourceScene': str(scene), 'sourceSceneSha256': sha(scene), 'colorSha256': sha(output / 'color.raw'),
        'color': 'actual D3D11 GPU-generated SDR proxy of new prewarp HDR; NOT old postwarp output',
        'depthGuideSha256': sha(output / 'depth.raw'), 'motionGuideSha256': sha(output / 'motion.raw'),
        'guideProvenance': 'New prewarp32bitdepth and velocity matched exact earlier normalization sources; normalized depth exact and backward pixel motion conversion rechecked.',
        'stencil': 'Not treated as32bitdepth; stage-specific stencil may differ',
        'size': [2560, 1440], 'reset': 1, 'jitter': [0, 0], 'depthInverted': True,
        'limits': 'Single actual capture Reset input; no fabricated history, no complete transparent/temporal guide acceptance.'
    }
    (output / 'input.json').write_text(json.dumps(record, indent=2), encoding='utf8')
    print(json.dumps(record, indent=2))


if __name__ == '__main__':
    assert back_motion(.5, -.25, 100, 80) == (-25, -10)
    main()
