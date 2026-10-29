"""Independent artifact check for the color-only D3D12 port; no GPU or model execution."""
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

source, baseline, run, report = map(Path, sys.argv[1:])
assert not report.exists(), 'Refusing previous report'
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'nr'))
from prepare_live_sr_pair import audited_frames

frames = audited_frames(source, json.loads((source / 'audit.json').read_text()))
assert [f['counter'] for f in frames] == list(range(698, 706))
status = json.loads((run.parent / (run.name + '-exit.json')).read_text())
assert status['status'] == 0 and not status['error'] and not status['signal']
assert not (run / 'INCOMPLETE').exists()
checks = json.loads((run / 'checks.json').read_text())
for key, value in {'frames': 8, 'exactStages': 48, 'inputReadbacksExact': 32, 'refusals': 6,
                   'pairRefusals': 3, 'invalidModelPixelsPreserved': 3, 'bothDebugErrors': 0,
                   'gpuBasedValidation': True, 'nrEvaluations': 0, 'srEvaluations': 0}.items():
    assert checks[key] == value, key
log = (run.parent / (run.name + '-process.log')).read_text()
rows = re.findall(r'ColorExact frame=(\d) mode=(neutral|actual-nr-fixture) gain=([\d.]+) stages=3 inputUnchanged=2', log)
assert [(int(i), mode) for i, mode, _ in rows] == [(i, mode) for i in range(8) for mode in ['neutral', 'actual-nr-fixture']]
assert log.count('PersistentColor12 proxy=') == 1 and 'PASS Color48/Inputs32/Refusals6/PairRefusals3/InvalidPixels3/Debug0 NR0 SR0' in log


def raw(path, width, height, fmt, bpp):
    data = path.read_bytes()
    assert len(data) == 16 + width * height * bpp and struct.unpack_from('<4I', data) == (width, height, fmt, bpp), str(path)
    return data[16:]


def crop(data):
    return b''.join(data[(y * 5120 + 1280) * 4:(y * 5120 + 1280 + 2560) * 4] for y in range(1440))


proof = []
reference_inputs = []
for i in range(8):
    scene_path = source / f'frame{i}-scene.raw'
    native = crop(raw(scene_path, 5120, 1440, 26, 4))
    exposure_path = source / f'frame{i}-exposure.raw'
    exposure = struct.unpack_from('<f', raw(exposure_path, 1, 1, 16, 8))[0]
    assert all(abs(float(g) - exposure) < 0.00001 for f, _, g in rows if int(f) == i)
    expected = {
        'proxy.raw': (10, 8, raw(baseline / f'nr-input/frame{i}/color.raw', 2560, 1440, 10, 8)),
        'neutral.raw': (26, 4, native),
        'restored.raw': (26, 4, crop(raw(baseline / f'gpu-restore/frame{i}/scene-restored.raw', 5120, 1440, 26, 4))),
        'neutral-sr.raw': (10, 8, raw(baseline / f'cpu-reference/frame{i}/color.raw', 2560, 1440, 10, 8)),
        'sr-input.raw': (10, 8, raw(baseline / f'sr-input/frame{i}/color.raw', 2560, 1440, 10, 8))}
    for name, (fmt, bpp, data) in expected.items():
        actual = raw(run / f'frame{i}' / name, 2560, 1440, fmt, bpp)
        assert actual == data, f'Color bytes differ: frame{i}/{name}'
        proof.append({'frame': i, 'file': name, 'sha256Payload': hashlib.sha256(actual).hexdigest(), 'exact': True})
    for p in [scene_path, exposure_path, baseline / f'nr-output/frame{i}/nr-output.raw']:
        reference_inputs.append({'file': str(p), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()})
expected_invalid = bytearray(crop(raw(baseline / 'gpu-restore/frame0/scene-restored.raw', 5120, 1440, 26, 4)))
expected_invalid[:12] = crop(raw(source / 'frame0-scene.raw', 5120, 1440, 26, 4))[:12]
assert raw(run / 'invalid-model.raw', 2560, 1440, 26, 4) == expected_invalid
result = {'kind': 'D3D12_COLOR_INDEPENDENT_CHECK', 'checks': checks, 'uniqueNormalFilesExact': len(proof),
          'proof': proof, 'referenceInputs': reference_inputs, 'invalidModelFullOutputExact': True,
          'proxyFormat': 'RGBA16F', 'nativeRestoredFormat': 'R11G11B10_FLOAT', 'srColorFormat': 'RGBA16F',
          'limits': 'Color-only. Real scene/exposure and historical NR outputs; no actual NR/SR, no interop/model integration, no game/performance/quality acceptance.'}
report.write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps({'uniqueNormalFilesExact': len(proof), 'stageComparisonsExact': 48, 'invalidModelFullOutputExact': True,
                  'debugErrors': 0, 'nrEvaluations': 0, 'srEvaluations': 0}, indent=2))
