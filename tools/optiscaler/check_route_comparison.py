"""Verify same-path direct correctness and bounded ABBA diagnostics; reject failed runs."""
import configparser
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import struct
import sys

project, root, output = map(Path, sys.argv[1:])
assert not output.exists(), 'Refusing previous report'
sys.path.insert(0, str(project / 'tools/nr'))
from prepare_live_sr_pair import audited_frames
source = project / 'artifacts/captures/live-eight-58000-130125640'
base = project / 'artifacts/research/live-nr-sr-eight'
frames = audited_frames(source, json.loads((source / 'audit.json').read_text()))
assert [f['counter'] for f in frames] == list(range(698, 706))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def success(name):
    status = json.loads((root / (name + '-exit.json')).read_text())
    assert status['status'] == 0 and not status['error'] and not status['signal'], name + ' failed'
    assert not (root / name / 'INCOMPLETE').exists(), name + ' incomplete'
    return (root / (name + '-process.log')).read_text()


def raw(path, width=2560, fmt=10, bpp=8):
    data = path.read_bytes()
    assert len(data) == 16 + width * 1440 * bpp and struct.unpack_from('<4I', data) == (width, 1440, fmt, bpp)
    return data[16:]


def crop(data):
    return b''.join(data[(y * 5120 + 1280) * 4:(y * 5120 + 3840) * 4] for y in range(1440))


loader = json.loads((root / 'loader-provenance.json').read_text())
assert loader['loaderSha256'] == sha(Path(loader['path'])) == 'bc43df9ec032004fde681d3e3afd01cde239e03a0125b822e88674c84287f99c'
assert sha(root / 'opti-host-v3/OptiScaler.dll') == 'c8c30d1485bfd9747ff0680956e2403b6f8296d5ae6d2da1c1e6cbd0633f9242'
config = configparser.RawConfigParser()
config.read(root / 'opti-host-v3/OptiScaler.ini', encoding='utf-8-sig')
assert config.getboolean('Menu', 'OverlayMenu')  # Default route; no swapchain/Present in this probe.
assert config.getboolean('Log', 'LogToFile') and config.getint('Log', 'LogLevel') == 6
assert not config.getboolean('Log', 'LogToConsole') and not config.getboolean('DlssNr', 'Enabled') and not config.getboolean('FrameGen', 'Enabled')
assert (root / 'opti-host-v3/OptiScaler.log').stat().st_size == 0
assert not (root / 'direct-host/OptiScaler.dll').exists()
log = success('direct-validation')
assert 'Route=direct-ngx' in log and 'ChainDebugErrors=0' in log
assert len(re.findall(r'^CHAIN_RESULT index=\d exactStages=5 inputUnchanged=4$', log, re.M)) == 8
assert len(re.findall(r'^BRIDGE_RESULT index=\d published=1 returnedExact=1 originalD11Inputs=4$', log, re.M)) == 8
proof = []
for i in range(8):
    for file, expected in [('proxy.raw', raw(base / f'nr-input/frame{i}/color.raw')),
                            ('nr-output.raw', raw(base / f'nr-output/frame{i}/nr-output.raw')),
                            ('restored.raw', crop(raw(base / f'gpu-restore/frame{i}/scene-restored.raw', 5120, 26, 4))),
                            ('sr-input.raw', raw(base / f'sr-input/frame{i}/color.raw')),
                            ('sr-output.raw', raw(base / f'history/frame{i}/sr-output.raw')),
                            ('returned11.raw', raw(base / f'history/frame{i}/sr-output.raw'))]:
        data = raw(root / f'direct-validation/frame{i}' / file, fmt=26 if file == 'restored.raw' else 10, bpp=4 if file == 'restored.raw' else 8)
        assert data == expected, f'Direct frame{i}/{file} mismatch'
        proof.append({'frame': i, 'file': file, 'sha256Payload': hashlib.sha256(data).hexdigest()})
keys = ['nrParamsCpuUs', 'nrEvaluateCpuUs', 'srParamsCpuUs', 'srEvaluateCpuUs', 'recordCpuUs', 'bridgeSubmitCpuUs',
        'completionPollWallUs', 'prepareGpuMs', 'nrGpuMs', 'restoreConvertGpuMs', 'srGpuMs']
sequence = ['opti-a-v3', 'direct-a-v3', 'direct-b-v3', 'opti-b-v3']
by_route = {'optiscaler': [], 'direct-ngx': []}
run_results = []
expected_final = (base / 'history/frame7/sr-output.raw').read_bytes()
for name in sequence:
    log = success(name)
    data = json.loads((root / name / 'measurement.json').read_text())
    route = 'optiscaler' if name.startswith('opti') else 'direct-ngx'
    assert data['route'] == route and data['bothDebugErrors'] == 0
    assert data['iterations'] == data['iterationsPerformed'] == data['nrEvaluations'] == data['srEvaluations'] == 40
    assert data['warmupIterations'] == 8 and data['measuredIterations'] == 32
    assert data['imageReadbacksDuringMeasuredLoop'] == 0 and data['timestampFrequency'] > 0
    assert data['debugLayers'] is True and data['performanceAccepted'] is False
    assert f'PASS Measurement route={route} NR40 SR40 samples32 warmup8 debug0 finalExact1' in log
    assert 'MEASURE_ATTEMPTS nr=40 sr=40 debugErrors=0' in log
    for stage in ['CreateChainNR', 'CreateChainSR', 'ReleaseNR', 'ReleaseSR', 'ShutdownChain']:
        assert log.count(stage + ' result=1') == 1
    loaded = re.findall(r'^LoadedChainModel name=(\S+) path=(.+)$', log, re.M)
    assert len(loaded) == 2
    for model, path in loaded:
        official = project / ('artifacts/research/official-nr/remix-sdk/lib/Windows_x86_64/rel/nvngx_dlssnr.dll' if model == 'nvngx_dlssnr.dll' else 'artifacts/research/official-sr/nvngx_dlss.dll')
        assert sha(Path(path)) == sha(official)
    assert (root / name / 'final-returned11.raw').read_bytes() == expected_final
    assert len(data['rows']) == 40
    for i, row in enumerate(data['rows']):
        assert row['iteration'] == i and row['frame'] == i % 8 and row['warmup'] == (i < 8) and row['reset'] == (i % 8 == 0)
        assert all(math.isfinite(row[k]) and row[k] >= 0 for k in keys)
        assert all(row[k] > 0 for k in ['nrEvaluateCpuUs', 'srEvaluateCpuUs', 'nrGpuMs', 'srGpuMs'])
        assert row['recordCpuUs'] >= sum(row[k] for k in ['nrParamsCpuUs', 'nrEvaluateCpuUs', 'srParamsCpuUs', 'srEvaluateCpuUs'])
    measured = data['rows'][8:]
    by_route[route].extend(measured)
    run_results.append({'run': name, 'route': route, 'sampleCount': 32,
                        'means': {k: statistics.mean(row[k] for row in measured) for k in keys},
                        'initAndCreateWithGpuWaitMs': data['initAndCreateWithGpuWaitUs'] / 1000,
                        'entryBindingUsNotComparable': data['entryBindingUs']})


def distribution(values):
    values = sorted(values)
    return {'count': len(values), 'mean': statistics.mean(values), 'median': statistics.median(values),
            'p95': values[math.ceil(len(values) * .95) - 1], 'min': values[0], 'max': values[-1]}


summaries = {}
for route, rows in by_route.items():
    assert len(rows) == 64
    summaries[route] = {k: distribution([row[k] for row in rows]) for k in keys}
    summaries[route]['nrSrEvaluateCpuUs'] = distribution([row['nrEvaluateCpuUs'] + row['srEvaluateCpuUs'] for row in rows])
    summaries[route]['colorModelsGpuMs'] = distribution([sum(row[k] for k in ['prepareGpuMs', 'nrGpuMs', 'restoreConvertGpuMs', 'srGpuMs']) for row in rows])
# Failed attempts must remain evidence and must not enter the accepted sample set.
failed_first = json.loads((root / 'opti-a-failed-accounting.json').read_text())
assert failed_first['nr'] == failed_first['sr'] == 40 and not failed_first['accepted']
failed_second = json.loads((root / 'opti-diagnostic-v2/measurement.json').read_text())
assert failed_second['iterationsPerformed'] == failed_second['nrEvaluations'] == failed_second['srEvaluations'] == 31
assert failed_second['bothDebugErrors'] == 1
for name in ['opti-a', 'opti-diagnostic-v2']:
    assert json.loads((root / (name + '-exit.json')).read_text())['status'] == 1
    assert (root / name / 'INCOMPLETE').exists()
result = {'kind': 'SAME_PATH_LIMITED_ROUTE_COMPARISON', 'directCorrectnessProof': proof, 'schedule': sequence,
          'runs': run_results, 'summary': summaries, 'acceptedMeasuredSamplesPerRoute': 64,
          'acceptedMeasurements': {'optiscaler': {'nr': 80, 'sr': 80}, 'directNgx': {'nr': 80, 'sr': 80}},
          'stageEvaluations': {'optiscaler': {'nr': 151, 'sr': 151}, 'directNgx': {'nr': 88, 'sr': 88}},
          'optiscalerCumulativeEvaluations': {'nr': 198, 'sr': 218}, 'failedOptiscalerAttemptsExcluded': 71,
          'directPublicLoaderCorrectnessExactStages': 40, 'directReturnedD11Exact': 8,
          'fullGamePerformanceAccepted': False, 'gpuCostIncludesD11Transfers': False,
          'limitations': ['Debug layers enabled; small correlated repeated-eight sample; not game workload or production frame latency',
                          'D3D12 timestamps bracket caller commands; no driver/model private queue instrumentation',
                          'No GPU timestamp for D11 uploads/return copy; bridgeSubmit is CPU only',
                          'RecordCPU includes common diagnostics/query/barrier recording; EvaluateCPU includes runtime/backend work and is not pure wrapper overhead',
                          'completionPollWall is diagnostic yield/fence time and cannot be converted to game FPS',
                          'Only final output checked during measurement; all-eight correctness separately qualified',
                          'Direct loader static startup occurs before main; entryBindingUs is not comparable to dynamic OptiScaler LoadLibrary',
                          'OverlayMenu=false selected legacy menu, not disable; default true bypasses it in this no-swapchain/Present probe']}
output.write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps({'samplesPerRoute': 64,
                  'cpuEvaluateMeanUs': {r: s['nrSrEvaluateCpuUs']['mean'] for r, s in summaries.items()},
                  'gpuColorModelsMeanMs': {r: s['colorModelsGpuMs']['mean'] for r, s in summaries.items()},
                  'stageEvaluations': result['stageEvaluations'], 'failedAttemptsExcluded': 71,
                  'gamePerformanceAccepted': False}, indent=2))
