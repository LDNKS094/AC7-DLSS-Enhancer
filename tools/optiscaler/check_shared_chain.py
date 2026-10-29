"""Validate final native-HDR shared-chain runs and preserved evaluation accounting."""
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

project, root, output = map(Path, sys.argv[1:])
assert not output.exists(), 'Refusing previous report'
sys.path.insert(0, str(project / 'tools/nr'))
from prepare_live_sr_pair import audited_frames
source = project / 'artifacts/captures/live-eight-58000-130125640'
base = project / 'artifacts/research/live-nr-sr-eight'
reference = project / 'artifacts/research/nonblocking-chain-v1/run-v2'
frames = audited_frames(source, json.loads((source / 'audit.json').read_text()))
assert [f['counter'] for f in frames] == list(range(698, 706))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def raw(path, width=2560, fmt=10, bpp=8):
    data = path.read_bytes()
    assert len(data) == 16 + width * 1440 * bpp and struct.unpack_from('<4I', data) == (width, 1440, fmt, bpp), str(path)
    return data[16:]


def crop(data):
    return b''.join(data[(y * 5120 + 1280) * 4:(y * 5120 + 3840) * 4] for y in range(1440))


proof = []


def exact(run, frame, file, expected):
    data = raw(root / run / f'frame{frame}' / file, fmt=26 if file == 'restored.raw' else 10, bpp=4 if file == 'restored.raw' else 8)
    assert data == expected, f'{run}/frame{frame}/{file} differs'
    proof.append({'run': run, 'frame': frame, 'file': file, 'sha256Payload': hashlib.sha256(data).hexdigest()})


for run, fault, evaluated in [('normal-v2', False, list(range(8))), ('faults-v2', True, [0, 2, 3, 4, 5, 6, 7])]:
    status = json.loads((root / (run + '-exit.json')).read_text())
    assert status['status'] == 0 and not status['error'] and not status['signal']
    assert not (root / run / 'INCOMPLETE').exists()
    report = json.loads((root / run / 'chain-checks.json').read_text())
    assert report['sharedD11Bridge'] and report['faultTest'] == fault and report['bothDebugErrors'] == 0
    assert report['nrEvaluations'] == report['srEvaluations'] == len(evaluated)
    log = (root / (run + '-process.log')).read_text()
    backend = (root / (run + '-backend.log')).read_text(encoding='utf-8-sig')
    assert 'NATIVE_BRIDGE_EXTENT_REFUSED=1' in log and 'NATIVE_BRIDGE_INPUT_REFUSED=1' in log
    assert 'ChainDebugErrors=0' in log
    for name in ['CreateChainNR', 'CreateChainSR', 'ReleaseNR', 'ReleaseSR', 'ShutdownChain']:
        assert len(re.findall(r'^' + name + r' result=1$', log, re.M)) == 1
    for name in ['EvaluateChainNR', 'EvaluateChainSR']:
        assert len(re.findall(r'^' + name + r' result=1$', log, re.M)) == len(evaluated)
    rows = re.findall(r'^CHAIN_FRAME index=(\d) NRReset=(\d) SRReset=(\d) dtMs=([\d.]+) gain=([\d.]+) NR=(\w+) SR=(\w+) proxy=(\w+) output=(\w+) token=(\d+)', log, re.M)
    assert [int(r[0]) for r in rows] == evaluated
    resets = [1, 1, 1, 0, 0, 1, 0] if fault else [1] + [0] * 7
    assert [int(r[1]) for r in rows] == [int(r[2]) for r in rows] == resets
    assert all(row[5:9] == rows[0][5:9] for row in rows)
    for row in rows:
        i = int(row[0])
        assert int(row[9]) == (1 << 33) + i
        dt = struct.unpack('<f', (base / f'sr-input/frame{i}/frame-time.bin').read_bytes())[0]
        assert abs(float(row[3]) - dt) < 0.000001
    assert backend.count('Passthrough to native NGX CreateFeature for feature 18') == 1
    assert backend.count('Passthrough to native DLSS EvaluateFeature for handle') == len(evaluated)
    assert 'IFeature_Dx11wDx12' not in backend
    loaded = re.findall(r'^LoadedChainModel name=(\S+) path=(.+)$', log, re.M)
    assert len(loaded) == 2
    for name, path in loaded:
        expected = project / ('artifacts/research/official-nr/remix-sdk/lib/Windows_x86_64/rel/nvngx_dlssnr.dll' if name == 'nvngx_dlssnr.dll' else 'artifacts/research/official-sr/nvngx_dlss.dll')
        assert sha(Path(path)) == sha(expected)
    if not fault:
        assert 'SOURCE_OVERWRITE index=2 layouts=4 afterQueuedInputCopy=1' in log
        assert len(re.findall(r'^BRIDGE_RESULT index=\d published=1 returnedExact=1 originalD11Inputs=4$', log, re.M)) == 8
        for i in evaluated:
            exact(run, i, 'proxy.raw', raw(base / f'nr-input/frame{i}/color.raw'))
            exact(run, i, 'nr-output.raw', raw(base / f'nr-output/frame{i}/nr-output.raw'))
            exact(run, i, 'restored.raw', crop(raw(base / f'gpu-restore/frame{i}/scene-restored.raw', 5120, 26, 4)))
            exact(run, i, 'sr-input.raw', raw(base / f'sr-input/frame{i}/color.raw'))
            for file in ['sr-output.raw', 'returned11.raw']:
                exact(run, i, file, raw(base / f'history/frame{i}/sr-output.raw'))
    else:
        assert not (root / run / 'frame1').exists()
        assert 'FAULT_PRE_EXPIRED index=1 modelCalls=0' in log
        assert 'FAULT_RESET_CONTRACT_REFUSED=1' in log
        assert 'FAULT_MATRIX drops=3 preExpired=1 stoppedPending=1 D11RetirementPending=8 originalFallbacks=4' in log
        results = re.findall(r'^BRIDGE_RESULT index=(\d) published=(\d) returnedExact=1 originalD11Inputs=4$', log, re.M)
        assert results == [(str(i), str(int(i not in [2, 5, 7]))) for i in evaluated]
        for i in evaluated:
            exact(run, i, 'proxy.raw', raw(base / f'nr-input/frame{i}/color.raw'))
        for file, ref in [('nr-output.raw', base / 'nr-output/frame0/nr-output.raw'), ('sr-input.raw', base / 'sr-input/frame0/color.raw'), ('sr-output.raw', base / 'history/frame0/sr-output.raw'), ('returned11.raw', base / 'history/frame0/sr-output.raw')]:
            exact(run, 0, file, raw(ref))
        exact(run, 0, 'restored.raw', crop(raw(base / 'gpu-restore/frame0/scene-restored.raw', 5120, 26, 4)))
        for i in [3, 4, 6]:
            assert f'RECOVERY_REFERENCE index={i} NR=1 SRInput=1 SR=1' in log
            for file in ['nr-output.raw', 'sr-input.raw', 'sr-output.raw']:
                exact(run, i, file, raw(reference / f'frame{i}' / file))
            exact(run, i, 'returned11.raw', raw(reference / f'frame{i}/sr-output.raw'))
        seed = b'\x00\x7e' * (2560 * 1440 * 4)
        for i in [2, 5, 7]:
            exact(run, i, 'returned11.raw', seed)
legacy = json.loads((root / 'legacy-transport-exit.json').read_text())
assert legacy['status'] == 0
assert 'PASS shared D3D11->D3D12->D3D11:' in (root / 'legacy-transport-process.log').read_text()
# Include every preserved hardware model run, not only final passing versions.
counts = {'nr': 0, 'sr': 0}
for name in ['normal-v1', 'faults-v1', 'normal-v2', 'faults-v2']:
    log = (root / (name + '-process.log')).read_text()
    for key, stage in [('nr', 'EvaluateChainNR'), ('sr', 'EvaluateChainSR')]:
        counts[key] += len(re.findall(r'^' + stage + r' result=1$', log, re.M))
assert counts == {'nr': 30, 'sr': 30}
result = {'kind': 'PUBLIC_OPTISCALER_NATIVE_HDR_SHARED_CHAIN', 'proof': proof,
          'normalExactStages': 40, 'normalReturnedD11Exact': 8, 'normalD12InputsExact': 32,
          'normalD11InputsReadbacks': 32, 'sourceOverwrittenLayouts': 4,
          'faultResetReferenceModelOutputs': 6, 'faultSrInputReferencesExact': 3,
          'unchangedLatePrivateDestinations': 3, 'originalFallbackFrames': [1, 2, 5, 7],
          'd11RetirementGatedPolls': 8, 'stoppedOwnerReplacedAfterDrain': True,
          'evaluationsThisStage': counts, 'optiscalerCumulative': {'nr': 47, 'sr': 67},
          'legacyFp16TransportRegressionPassed': True, 'bothDebugErrors': 0,
          'limits': 'Diagnostic normalized guides initialized from qualified fixtures. Publication uses private D11 output; caller must never pass game scene. No game hook, true API/device failure, resize, hot unload, FG, compression, quality or performance acceptance; whole-list abandonment does not prevent already-submitted SR work.'}
output.write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps({k: result[k] for k in ['normalExactStages', 'normalReturnedD11Exact', 'faultResetReferenceModelOutputs', 'unchangedLatePrivateDestinations', 'd11RetirementGatedPolls', 'evaluationsThisStage', 'optiscalerCumulative']}, indent=2))
