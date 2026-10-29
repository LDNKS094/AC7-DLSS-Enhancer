"""Verify operation isolation and the final deferred-copy ASI path on both device types."""
import hashlib
import json
from pathlib import Path
import struct
import sys

source, root, output = map(Path, sys.argv[1:])
assert not output.exists(), 'Refusing previous report'
scene = source.read_bytes()
assert len(scene) == 16 + 5120 * 1440 * 4 and struct.unpack_from('<4I', scene) == (5120, 1440, 26, 4)


def success(name):
    status = json.loads((root / (name + '-exit.json')).read_text())
    assert status['status'] == 0 and not status['signal'] and not status['error'], name
    assert not (root / name / 'INCOMPLETE').exists(), name
    return json.loads((root / name / 'checks.json').read_text())


proof = []
for stage in [0, 1, 2, 5]:
    name = f'stage{stage}'
    checks = success(name)
    assert checks['diagnosticStage'] == stage and checks['originalDrawCalls'] == 3
    assert checks['originalPipelineQueryDraws'] == 4 and checks['stateSnapshotsExact'] and checks['drained']
    assert checks['applied'] == int(stage != 0) and not checks['debugLayerEnabled']
    run = root / name
    assert (run / 'hooked-scene.raw').read_bytes() == scene
    baseline = (run / 'baseline-draw.raw').read_bytes()
    assert (run / 'probe-enabled-draw.raw').read_bytes() == baseline
    assert (run / 'probe-stopped-draw.raw').read_bytes() == baseline
failure = json.loads((root / 'stage3-exit.json').read_text())
assert failure['status'] == 10 and (root / 'stage3/INCOMPLETE').exists()
failed_log = (root / 'stage3-process.log').read_text()
assert 'ASI_PIPELINE_TIMEOUT file=probe-enabled-draw.raw removedReason=-2005270522' in failed_log
assert 'TIMEOUT_STATUS applied=1 intercepted=2 original=2 drained=1 error=0' in failed_log
for name, debug in [('ordinary-final', False), ('debug-final', True)]:
    checks = success(name)
    assert checks['debugLayerEnabled'] == debug
    for key, value in {'actualLoadLibrary': True, 'dllMainNoHook': True, 'actualMinHookInstall': True,
                       'shaderMatches': 1, 'defaultDisabled': True, 'applied': 2, 'refused': 2,
                       'originalHookedDrawCalls': 8, 'originalPipelineQueryDraws': 7, 'exactDrawOutputs': 6,
                       'fullSceneExact': True, 'bindingSnapshotsExact': True, 'stopPendingChecks': 8,
                       'stoppedDrained': True, 'debugErrors': 0, 'modelEvaluations': 0,
                       'actualGame': False, 'hotUnload': False}.items():
        assert checks[key] == value, f'{name}/{key}'
    run = root / name
    assert (run / 'hooked-scene.raw').read_bytes() == scene
    baseline = (run / 'baseline-draw.raw').read_bytes()
    assert len(baseline) == len(scene) and baseline[:16] == scene[:16]
    for y in range(1440):
        offset = 16 + (y * 5120 + 1280) * 4
        assert baseline[offset:offset + 2560 * 4] == scene[offset:offset + 2560 * 4]
    for file in ['baseline-draw.raw', 'default-disabled-draw.raw', 'enabled-draw.raw', 'duplicate-token-draw.raw',
                 'explicit-disabled-draw.raw', 'stopped-draw.raw']:
        data = (run / file).read_bytes()
        assert data == baseline, f'{name}/{file}'
        proof.append({'run': name, 'file': file, 'sha256': hashlib.sha256(data).hexdigest()})
    assert (run / 'invalid-viewport-draw.raw').read_bytes() != baseline
    assert 'PASS ASI load/hooks/CreatePS/originalDraw8/neutral2/state/stopPending8/drain/debug0 model0 game0' in (root / (name + '-process.log')).read_text()
core = success('core-final')
assert core['hiddenCsUavCounterExact'] and core['originalComputeContinued'] and core['gatedPendingChecks'] == 8
assert core['sceneFullBitExact'] and core['debugErrors'] == 0
assert (root / 'core-final/neutral-scene.raw').read_bytes() == scene
replacement = success('replacement-final')
assert replacement['shaderReplacementStopped'] and replacement['firstError'] == 5 and replacement['originalHookedDrawCalls'] == 3
assert replacement['sceneExact'] and replacement['drained'] and replacement['debugErrors'] == 0
assert (root / 'replacement-final/hooked-scene.raw').read_bytes() == scene
result = {'kind': 'NEUTRAL_ASI_DEFERRED_COPY_FIX', 'isolatedForwardGettersSwapPassed': [0, 1, 2],
          'directCopyStageFailedBeforeFence': True, 'failureRemovedReason': '0x887A0006 DXGI_ERROR_DEVICE_HUNG',
          'deferredProbePassed': True, 'ordinaryDeviceAsiSyntheticQualified': True, 'debugDeviceAsiSyntheticQualified': True,
          'perDeviceOriginalDrawCalls': 8, 'perDeviceActualPipelineQueryDraws': 7, 'perDeviceExactDrawOutputs': 6,
          'perDeviceNeutralApplications': 2, 'perDeviceStopPendingChecks': 8,
          'coreCounterAndDispatchRegressionPassed': True, 'replacementFailClosedRegressionPassed': True,
          'proof': proof, 'modelEvaluations': 0, 'deployableGamePackage': False,
          'limits': 'Functional synthetic-host qualification only. Ordinary run has no debug layer; debug run reports zero errors. No actual game loader, main-view/guide/frame source, rendering-thread performance, hot unload or native device-error recovery. Direct-copy hang root below the callback path is unknown; no claim of a driver fix.'}
output.write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps({k: result[k] for k in ['isolatedForwardGettersSwapPassed', 'directCopyStageFailedBeforeFence',
                                       'ordinaryDeviceAsiSyntheticQualified', 'debugDeviceAsiSyntheticQualified',
                                       'coreCounterAndDispatchRegressionPassed', 'replacementFailClosedRegressionPassed',
                                       'deployableGamePackage', 'modelEvaluations']}, indent=2))
