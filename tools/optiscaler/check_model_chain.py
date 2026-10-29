"""Check independent NR plus both same-queue chains without running GPU/model code."""
import configparser
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

project, solo, chain, output = map(Path, sys.argv[1:])
assert not output.exists(), 'Refusing previous report'
sys.path.insert(0, str(project / 'tools/nr'))
from prepare_live_sr_pair import audited_frames
source = project / 'artifacts/captures/live-eight-58000-130125640'
base = project / 'artifacts/research/live-nr-sr-eight'
frames = audited_frames(source, json.loads((source / 'audit.json').read_text()))
assert [f['counter'] for f in frames] == list(range(698, 706))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def successful(root, name):
    status = json.loads((root / (name + '-exit.json')).read_text())
    assert status['status'] == 0 and not status['error'] and not status['signal']
    assert not (root / name / 'INCOMPLETE').exists()
    return (root / (name + '-process.log')).read_text()


def raw(path, width, height, fmt, bpp):
    data = path.read_bytes()
    assert len(data) == 16 + width * height * bpp and struct.unpack_from('<4I', data) == (width, height, fmt, bpp), str(path)
    return data[16:]


def crop(data):
    return b''.join(data[(y * 5120 + 1280) * 4:(y * 5120 + 3840) * 4] for y in range(1440))


stock = 'c8c30d1485bfd9747ff0680956e2403b6f8296d5ae6d2da1c1e6cbd0633f9242'
assert sha(solo / 'host/OptiScaler.dll') == sha(chain / 'host/OptiScaler.dll') == stock
official = {'nvngx_dlssnr.dll': project / 'artifacts/research/official-nr/remix-sdk/lib/Windows_x86_64/rel/nvngx_dlssnr.dll',
            'nvngx_dlss.dll': project / 'artifacts/research/official-sr/nvngx_dlss.dll'}
for name, path in official.items():
    assert sha(solo / 'host/OptiScaler' / name) == sha(chain / 'host/OptiScaler' / name) == sha(path)
config = configparser.RawConfigParser()
config.read(chain / 'host/OptiScaler.ini', encoding='utf-8-sig')
assert not config.getboolean('DlssNr', 'Enabled') and not config.getboolean('FrameGen', 'Enabled')
assert config.get('Upscalers', 'Dx12Upscaler') == 'dlss'
log = successful(solo, 'run-v1')
assert 'NRCapability result=1 available=1' in log and 'BothDebugErrors=0 FrameCount=1' in log
for stage in ['Init12', 'CreateFeature12', 'EvaluateFeature12', 'ReleaseFeature12', 'DestroyParameters12', 'Shutdown12']:
    assert log.count(stage + ' result=0x1') == 1
assert len(re.findall(r'^InputExact index=', log, re.M)) == 4
assert (solo / 'run-v1/native-nr-complete.txt').exists()
assert (solo / 'run-v1/nr-output.raw').read_bytes() == (base / 'nr-output/frame0/nr-output.raw').read_bytes()
solo_backend = (solo / 'run-v1-backend.log').read_text(encoding='utf-8-sig')
assert 'Passthrough to native NGX CreateFeature for feature 18' in solo_backend
assert 'Passthrough to native DLSS EvaluateFeature for handle' in solo_backend

proof = []
for name, sr_reset, sr_reference in [('history-v1', False, 'history'), ('sr-reset-v1', True, 'reset-control')]:
    run = chain / name
    log = successful(chain, name)
    backend = (chain / (name + '-backend.log')).read_text(encoding='utf-8-sig')
    report = json.loads((run / 'chain-checks.json').read_text())
    for key, value in {'frames': 8, 'exactStages': 40, 'inputReadbacksExact': 32, 'nrEvaluations': 8, 'srEvaluations': 8,
                       'bothDebugErrors': 0, 'singleD3d12Queue': True, 'intermediateCpuImageFeedback': False,
                       'srResetEveryFrame': sr_reset}.items():
        assert report[key] == value, key
    for stage in ['InitChain', 'CreateChainNR', 'CreateChainSR', 'ReleaseSR', 'ReleaseNR', 'ShutdownChain']:
        assert len(re.findall(r'^' + stage + r' result=1$', log, re.M)) == 1
    for stage in ['EvaluateChainNR', 'EvaluateChainSR']:
        assert len(re.findall(r'^' + stage + r' result=1$', log, re.M)) == 8
    assert 'ChainDebugErrors=0' in log
    rows = re.findall(r'^CHAIN_FRAME index=(\d) NRReset=(\d) SRReset=(\d) dtMs=([\d.]+) gain=([\d.]+) NR=(\w+) SR=(\w+) proxy=(\w+) output=(\w+)', log, re.M)
    assert [int(r[0]) for r in rows] == list(range(8))
    assert [int(r[1]) for r in rows] == [1] + [0] * 7
    assert [int(r[2]) for r in rows] == ([1] * 8 if sr_reset else [1] + [0] * 7)
    for i, row in enumerate(rows):
        dt = struct.unpack('<f', (base / f'sr-input/frame{i}/frame-time.bin').read_bytes())[0]
        exposure = struct.unpack('<f', raw(source / f'frame{i}-exposure.raw', 1, 1, 16, 8)[:4])[0]
        assert abs(float(row[3]) - dt) < 0.000001 and abs(float(row[4]) - exposure) < 0.00000001
        assert row[5:] == rows[0][5:], 'Feature/resource identity changed'
    assert len(re.findall(r'^CHAIN_RESULT index=\d exactStages=5 inputUnchanged=4$', log, re.M)) == 8
    assert backend.count('Passthrough to native NGX CreateFeature for feature 18') == 1
    assert backend.count('Passthrough to native DLSS EvaluateFeature for handle') == 8
    assert backend.count('DLSSFeatureDx12::InitDLSS _CreateFeature result: NVSDK_NGX_Result_Success') == 1
    assert 'IFeature_Dx11wDx12' not in backend
    for i in range(8):
        tests = [('proxy.raw', 10, 8, base / f'nr-input/frame{i}/color.raw'),
                 ('nr-output.raw', 10, 8, base / f'nr-output/frame{i}/nr-output.raw'),
                 ('restored.raw', 26, 4, base / f'gpu-restore/frame{i}/scene-restored.raw'),
                 ('sr-input.raw', 10, 8, base / f'sr-input/frame{i}/color.raw'),
                 ('sr-output.raw', 10, 8, base / sr_reference / f'frame{i}/sr-output.raw')]
        for file, fmt, bpp, reference in tests:
            expected = crop(raw(reference, 5120, 1440, fmt, bpp)) if file == 'restored.raw' else raw(reference, 2560, 1440, fmt, bpp)
            data = raw(run / f'frame{i}' / file, 2560, 1440, fmt, bpp)
            assert data == expected, f'{name}/frame{i}/{file} differs from qualified direct baseline'
            proof.append({'run': name, 'frame': i, 'file': file, 'sha256Payload': hashlib.sha256(data).hexdigest(), 'exact': True})
reset_log = (chain / 'sr-reset-v1-process.log').read_text()
loaded = re.findall(r'^LoadedChainModel name=(\S+) path=(.+)$', reset_log, re.M)
assert len(loaded) == 2
for name, path in loaded:
    assert name in official and sha(Path(path)) == sha(official[name])
for i in range(8):
    for file in ['proxy.raw', 'nr-output.raw', 'restored.raw', 'sr-input.raw']:
        assert (chain / f'history-v1/frame{i}' / file).read_bytes() == (chain / f'sr-reset-v1/frame{i}' / file).read_bytes()
assert (chain / 'history-v1/frame0/sr-output.raw').read_bytes() == (chain / 'sr-reset-v1/frame0/sr-output.raw').read_bytes()
assert any((chain / f'history-v1/frame{i}/sr-output.raw').read_bytes() != (chain / f'sr-reset-v1/frame{i}/sr-output.raw').read_bytes() for i in range(1, 8))
result = {'kind': 'PUBLIC_OPTISCALER_NR_AND_D3D12_CHAIN_CHECK', 'soloNrExact': True, 'proof': proof,
          'chainStagesExact': 80, 'chainInputReadbacksExact': 64, 'soloInputReadbacksExact': 4,
          'evaluationsThisStage': {'nr': 17, 'sr': 16}, 'optiscalerCumulativeEvaluations': {'nr': 17, 'sr': 37},
          'loadedModelFilesVerified': dict(loaded), 'nrHistoryUnaffectedBySrReset': True,
          'singleCallerD3d12QueueAndList': True, 'intermediateCpuImageFeedback': False,
          'internalOptiscalerNrPipelineUsed': False, 'deadlineAbandonmentQualified': False,
          'liveD3d11InteropQualified': False, 'performanceAccepted': False, 'gameIntegration': False,
          'limits': 'Files initialize native HDR and normalized guides. Same-size HDR/DLAA, P1 uncalibrated, jitter0. Caller queue/list only; NGX internal queues not inspected. Completion-only cleanup; no in-flight stop, real API/device loss, hot unload, compression, actual upscale, FG or quality acceptance.'}
output.write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps({k: result[k] for k in ['soloNrExact', 'chainStagesExact', 'chainInputReadbacksExact', 'evaluationsThisStage', 'optiscalerCumulativeEvaluations', 'performanceAccepted']}, indent=2))
