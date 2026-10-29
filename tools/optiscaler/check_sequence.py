"""Validate real fixed-eight public OptiScaler SR history/reset controls; never load a DLL."""
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys


def verify_source(root):
    contract = json.loads((root / 'sequence-contract.json').read_text())
    frames = contract['frames']
    assert contract['kind'] == 'REAL_NR_RESTORED_PAIR_SR_HISTORY' and contract['nrApplied'] is True
    assert len(frames) == 8 and contract['preExposure'] == 1 and contract['exposureScale'] == 1 and contract['jitter'] == [0, 0]
    capture = Path(contract['source'])
    previous = None
    result = []
    f32 = lambda x: struct.unpack('<f', struct.pack('<f', x))[0]
    for i, frame in enumerate(frames):
        assert frame['index'] == i and frame['sourceFrameCandidate'] == 698 + i
        for name, fmt, bpp in [('color.raw', 10, 8), ('depth.raw', 41, 4), ('motion.raw', 34, 4), ('exposure.raw', 41, 4), ('frame-time.bin', None, None)]:
            raw = (root / f'frame{i}' / name).read_bytes()
            assert hashlib.sha256(raw).hexdigest() == frame['hashes'][name], name
            if fmt is not None:
                width, height = (1, 1) if name == 'exposure.raw' else (2560, 1440)
                assert struct.unpack_from('<4I', raw) == (width, height, fmt, bpp)
                assert len(raw) == 16 + width * height * bpp
        dt_raw = (root / f'frame{i}/frame-time.bin').read_bytes()
        assert len(dt_raw) == 4
        dt = struct.unpack('<f', dt_raw)[0]
        view = (capture / f'frame{i}-view.bin').read_bytes()
        assert len(view) == 4096 and struct.unpack_from('<I', view, 137 * 16 + 4)[0] == 698 + i
        now = struct.unpack_from('<4f', view, 136 * 16)
        before = struct.unpack_from('<4f', view, 133 * 16)
        assert math.isfinite(dt) and dt > 0 and dt == f32(f32(now[2] - before[2]) * 1000)
        assert struct.unpack_from('<4f', view, 114 * 16) == (0, 0, 0, 0)
        if previous:
            assert before[2:] == previous[2:] and all(now[c] > previous[c] for c in [2, 3])
        previous = now
        result.append({'index': i, 'counter': 698 + i, 'dtMs': dt})
    return result


source = Path(sys.argv[1])
frames = verify_source(source)
if sys.argv[2:] == ['--preflight']:
    print(json.dumps({'sourceHashAndViewBinding': True, 'frames': frames}, indent=2))
    raise SystemExit(0)
history, reset, direct, out = map(Path, sys.argv[2:])
assert not out.exists(), 'Refusing previous report'
proof = []
for run, reset_all, baseline_name in [(history, False, 'history'), (reset, True, 'reset-control')]:
    exit_status = json.loads((run.parent / (run.name + '-exit.json')).read_text())
    assert exit_status['status'] == 0 and not exit_status['error'] and not exit_status['signal']
    assert (run / 'native-eight-complete.txt').exists() and not (run / 'INCOMPLETE').exists()
    log = (run.parent / (run.name + '-process.log')).read_text()
    backend = (run.parent / (run.name + '-backend.log')).read_text(encoding='utf-8-sig')
    assert 'FeatureLevel=0xb000' in log and 'BothDebugErrors=0 FrameCount=8' in log
    for stage in ['Init12', 'GetCapabilityParameters12', 'CreateFeature12', 'ReleaseFeature12', 'DestroyParameters12', 'Shutdown12']:
        assert len(re.findall(r'^' + stage + r' result=0x1', log, re.M)) == 1
    assert len(re.findall(r'^EvaluateFeature12 result=0x1', log, re.M)) == 8
    assert len(re.findall(r'^InputExact index=', log, re.M)) == 32
    rows = list(re.finditer(r'^SEQUENCE_FRAME index=(\d+) Reset=(\d) dtMs=([\d.]+) feature=(\w+) color=(\w+) output=(\w+)', log, re.M))
    assert [int(m[1]) for m in rows] == list(range(8))
    assert [int(m[2]) for m in rows] == ([1] * 8 if reset_all else [1] + [0] * 7)
    for m, frame in zip(rows, frames):
        assert abs(float(m[3]) - frame['dtMs']) < 0.0001
        assert all(m[c] == rows[0][c] for c in [4, 5, 6]), 'Feature/texture identity changed'
    assert len(re.findall(r'^SEQUENCE_RESULT index=\d+ finiteRgb=11059200 max=[\d.]+ inputReadbacksExact=4', log, re.M)) == 8
    assert 'Upscalers.Dx12Upscaler: dlss' in backend and 'DLSS v310.9.16 loaded.' in backend
    assert 'IFeature_Dx11wDx12' not in backend
    assert len(re.findall(r'NVSDK_NGX_D3D12_EvaluateFeature EvaluateFeature - Handle:', backend)) == 8
    assert len(re.findall(r'DLSSFeatureDx12::InitDLSS _CreateFeature result: NVSDK_NGX_Result_Success', backend)) == 1
    for i in range(8):
        data = (run / f'frame{i}/sr-output.raw').read_bytes()
        assert data == (direct / baseline_name / f'frame{i}/sr-output.raw').read_bytes(), f'{run.name} frame{i} differs from qualified direct NGX'
        proof.append({'mode': run.name, 'frame': i, 'sha256': hashlib.sha256(data).hexdigest(), 'wholeRawEqualsDirectNgx': True})
assert (history / 'frame0/sr-output.raw').read_bytes() == (reset / 'frame0/sr-output.raw').read_bytes()
assert any((history / f'frame{i}/sr-output.raw').read_bytes() != (reset / f'frame{i}/sr-output.raw').read_bytes() for i in range(1, 8)), 'No observable history/reset response'
result = {'kind': 'PUBLIC_D3D12_FIXED_EIGHT_PERSISTENT_SR_HISTORY', 'frames': frames, 'proof': proof,
          'inputReadbacksExact': 64, 'featuresCreated': 2, 'featureAndBridgeIdentityStable': True,
          'bothDebugErrorsAfterCleanup': 0, 'evaluations': {'sr': 16, 'nr': 0},
          'nativeD3d11FeatureLevel': '11.0', 'ngxResetControls': {'history': [1] + [0] * 7, 'reset': [1] * 8},
          'singleFrameProbeRegressionIncluded': False, 'gameIntegration': False, 'performanceAccepted': False,
          'limits': 'Two fresh isolated processes, one persistent feature/bridge each. Real audited source counters698-705, P1 uncalibrated, jitter0, same-size HDR/DLAA. No NR/FG, actual game state, repeated Init/Shutdown in one process, device-loss, hot unload, temporal quality or upscaling acceptance.'}
out.write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps({'frames': 8, 'wholeRawExactOutputs': 16, 'inputReadbacksExact': 64, 'persistentFeatures': 2,
                  'bothDebugErrorsAfterCleanup': 0, 'evaluations': result['evaluations'], 'performanceAccepted': False}, indent=2))
