"""Bounded ASI debug-host evidence check; ordinary-device failures are mandatory residual blockers."""
import hashlib
import json
from pathlib import Path
import struct
import sys

source, root, output = map(Path, sys.argv[1:])
assert not output.exists(), 'Refusing previous report'
run = root / 'debug-final'
status = json.loads((root / 'debug-final-exit.json').read_text())
assert status['status'] == 0 and not status['error'] and not status['signal']
assert not (run / 'INCOMPLETE').exists()
checks = json.loads((run / 'checks.json').read_text())
for key, value in {'actualLoadLibrary': True, 'dllMainNoHook': True, 'actualMinHookInstall': True,
                   'shaderMatches': 1, 'defaultDisabled': True, 'applied': 2, 'refused': 2,
                   'originalHookedDrawCalls': 8, 'originalPipelineQueryDraws': 7, 'exactDrawOutputs': 6,
                   'fullSceneExact': True, 'bindingSnapshotsExact': True, 'stopPendingChecks': 8,
                   'stoppedDrained': True, 'debugLayerEnabled': True, 'debugErrors': 0,
                   'modelEvaluations': 0, 'actualGame': False, 'hotUnload': False}.items():
    assert checks[key] == value, key
scene = source.read_bytes()
assert struct.unpack_from('<4I', scene) == (5120, 1440, 26, 4) and len(scene) == 16 + 5120 * 1440 * 4
assert (run / 'hooked-scene.raw').read_bytes() == scene
baseline = (run / 'baseline-draw.raw').read_bytes()
assert len(baseline) == len(scene) and baseline[:16] == scene[:16]
for y in range(1440):
    start = 16 + (y * 5120 + 1280) * 4
    assert baseline[start:start + 2560 * 4] == scene[start:start + 2560 * 4]
for name in ['default-disabled-draw.raw', 'enabled-draw.raw', 'duplicate-token-draw.raw', 'explicit-disabled-draw.raw', 'stopped-draw.raw']:
    assert (run / name).read_bytes() == baseline, name
assert (run / 'invalid-viewport-draw.raw').read_bytes() != baseline
replacement = json.loads((root / 'replacement-v1/checks.json').read_text())
assert replacement['shaderReplacementStopped'] and replacement['firstError'] == 5
assert replacement['originalHookedDrawCalls'] == 3 and replacement['debugErrors'] == 0 and replacement['drained']
assert json.loads((root / 'replacement-v1-exit.json').read_text())['status'] == 0
assert not (root / 'replacement-v1/INCOMPLETE').exists()
for name in ['core-release-v1', 'core-active-query-v1']:
    assert json.loads((root / (name + '-exit.json')).read_text())['status'] == 0
    assert not (root / name / 'INCOMPLETE').exists()
    assert (root / name / 'neutral-scene.raw').read_bytes() == scene
for name, code in [('release-v1', 10), ('release-v2', 1), ('release-v3', 1)]:
    assert json.loads((root / (name + '-exit.json')).read_text())['status'] == code
    assert (root / name / 'INCOMPLETE').exists()
result = {'kind': 'NEUTRAL_ASI_DEBUG_HOST_ONLY', 'debugHookMatrixPassed': True,
          'originalHookedDrawCalls': 8, 'realPipelineStatisticsDraws': 7, 'drawOutputsExact': 6,
          'fullSceneExact': True, 'shaderReplacementFailClosed': True, 'coreOrdinaryDevicePassed': True,
          'activeQueryCorePassed': True, 'ordinaryDeviceAsiQualified': False,
          'ordinaryDeviceFailures': ['release-v1: completion watchdog exit10', 'release-v2: dummy vtable did not preserve intercepted-call matrix', 'release-v3: actual-context vtable missed later draw'],
          'deployableGamePackage': False, 'gameStarted': False, 'modelEvaluations': 0,
          'proofHashes': {name: hashlib.sha256((run / name).read_bytes()).hexdigest() for name in ['baseline-draw.raw', 'enabled-draw.raw', 'hooked-scene.raw']},
          'limits': 'Only debug device actual MinHook CreatePS/DrawIndexed path passed; tests create resources and drive explicit tokens. No real loader invocation, View/guide identity, actual game, hook removal/hot unload or model/performance acceptance. Module and owner pinned until process termination.'}
output.write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps({k: result[k] for k in ['debugHookMatrixPassed', 'originalHookedDrawCalls', 'realPipelineStatisticsDraws', 'drawOutputsExact', 'ordinaryDeviceAsiQualified', 'deployableGamePackage', 'modelEvaluations']}, indent=2))
