"""Check saved draw/scene bytes and authoritative View counters; no GPU rerun."""
import hashlib
import json
from pathlib import Path
import struct
import sys

capture, root, output = map(Path, sys.argv[1:])
assert not output.exists(), 'Refusing previous report'

def success(name):
    status = json.loads((root / (name + '-exit.json')).read_text())
    assert status['status'] == 0 and not status.get('error'), name
    assert not (root / name / 'INCOMPLETE').exists(), name
    return json.loads((root / name / 'checks.json').read_text())

negatives = ['missing-guide', 'wrong-view-object', 'wrong-depth-object', 'changed-view-bytes',
             'invalid-view-replaces-cache', 'view-range-offset', 'gpu-copy-invalidates',
             'gpu-region-invalidates', 'gpu-region1-invalidates', 'unknown-command-list-invalidates',
             'ambiguous-motion', 'stale-guide-counter']
proof = []
for name, debug, disabled in [('ordinary-v2-final', False, False), ('debug-v2-final', True, False),
                              ('disabled-v2-final', True, True)]:
    checks = success(name)
    assert checks['observedFrames'] == 8 and checks['applied'] == (0 if disabled else 8)
    assert checks['negativeCases'] == 12 and checks['defaultDisabledTrial'] == disabled
    assert checks['debugLayer'] == debug and checks['debugErrors'] == 0
    for key in ['allScenesAndDrawsExact', 'gpuViewMatchesObservedCpu', 'counterGapWrapBackwardUnitControls', 'boundedStoppedDrained']:
        assert checks[key], f'{name}/{key}'
    assert not checks['externalFrameTokensUsed'] and not checks['actualGame'] and checks['modelEvaluations'] == 0
    run = root / name
    records = [json.loads(line) for line in (run / 'sequence.jsonl').read_text().splitlines()]
    assert len(records) == 8
    baseline = (run / 'baseline-draw.raw').read_bytes()
    assert struct.unpack_from('<4I', baseline) == (5120, 1440, 26, 4)
    assert len(baseline) == 16 + 5120 * 1440 * 4
    for negative in negatives:
        assert (run / (negative + '.raw')).read_bytes() == baseline, f'{name}/{negative}'
    for frame, record in enumerate(records):
        view = (capture / f'frame{frame}-view.bin').read_bytes()
        counter = struct.unpack_from('<I', view, 137 * 16 + 4)[0]
        assert record['counter'] == record['token'] == counter == 698 + frame
        assert record['applied'] == (0 if disabled else frame + 1)
        assert record['stopped'] == (1 if frame == 7 else 0)
        assert record['upload'] == ['UpdateSubresource', 'MapUnmap', 'UpdateSubresource1'][frame % 3]
        scene = (capture / f'frame{frame}-scene.raw').read_bytes()
        assert (run / f'frame{frame}-scene.raw').read_bytes() == scene
        expected = bytearray(baseline)
        for y in range(1440):
            start = 16 + (y * 5120 + 1280) * 4
            expected[start:start + 2560 * 4] = scene[start:start + 2560 * 4]
        draw = (run / f'frame{frame}-draw.raw').read_bytes()
        assert draw == expected, f'{name}/frame{frame}'
        proof.append({'run': name, 'frame': frame, 'sceneSha256': hashlib.sha256(scene).hexdigest(),
                      'drawSha256': hashlib.sha256(draw).hexdigest(), 'viewSha256': hashlib.sha256(view).hexdigest()})
        if frame == 0:
            assert (run / 'duplicate-counter.raw').read_bytes() == expected
        if frame == 7:
            assert (run / 'stopped-draw.raw').read_bytes() == expected
pending = success('pending-final')
for key, value in {'observedFrames': 3, 'applied': 2, 'pending': 1, 'firstCounter': 698, 'droppedCounter': 699,
                   'recoveredCounter': 700, 'negativeCases': 12, 'noLateSameFrameCopy': True,
                   'sceneAndDrawExact': True, 'stoppedDrained': True, 'debugErrors': 0,
                   'externalFrameTokensUsed': False, 'modelEvaluations': 0}.items():
    assert pending[key] == value, key
assert (root / 'pending-final/pending-scene.raw').read_bytes() == (capture / 'frame0-scene.raw').read_bytes()
base = (root / 'pending-final/baseline-draw.raw').read_bytes()
for name in ['duplicate-after-retirement', 'recovery', 'stopped'] + negatives:
    assert (root / 'pending-final' / (name + '.raw')).read_bytes() == base
for name in ['explicit-ordinary-final', 'explicit-debug-final']:
    checks = success(name)
    assert checks['originalHookedDrawCalls'] == 8 and checks['applied'] == 2 and checks['stopPendingChecks'] == 8
    assert checks['debugErrors'] == 0 and checks['fullSceneExact'] and checks['bindingSnapshotsExact']
for name, enabled in [('loader-defaultoff', False), ('loader-enabled', True)]:
    checks = success(name)
    assert checks['actualLoaderLoadedAsi'] and checks['actualLoaderInitializedAsi']
    assert not checks['manualInitializeAsiCalled'] and checks['defaultDisabled'] != enabled
    assert checks['hookInstalledBeforeHostDeviceAndTargetShaders'] and checks['guideCreatedBeforePrewarp']
    assert checks['targetShaderMatches'] == 2 and not checks['actualGame'] and not checks['actualGameStartupTimingQualified']
    assert (root / name / 'AC7NativeNeutral.asi').read_bytes() == (root / 'final.asi').read_bytes()
    assert (root / name / 'AC7NativeNeutral.enable').exists() == enabled
    logs = list((root / name).glob('AC7NativeNeutral-*.log'))
    assert len(logs) == 1 and 0 < len(logs[0].read_text().splitlines()) <= 64
    assert f'enabled={int(enabled)}' in logs[0].read_text().splitlines()[0]
logs = []
for path in root.glob('AC7NativeNeutral-*.log'):
    data = path.read_bytes()
    assert 0 < len(data.splitlines()) <= 64 and len(data) < 64 * 768
    logs.append({'file': path.name, 'sha256': hashlib.sha256(data).hexdigest(), 'lines': len(data.splitlines())})
result = {'kind': 'VIEW_UPLOAD_GUIDE_IDENTITY_INDEPENDENT_CHECK', 'ordinaryAndDebugEightFrameIdentity': True,
          'defaultDisabledEightObservationsZeroCopies': True, 'realCapturedCounters': list(range(698, 706)),
          'externalFrameTokensUsed': False, 'negativeCasesPerRun': 12, 'negativeImagesChecked': 48,
          'drawAndSceneComparisons': 48, 'gpuViewCpuByteMatch': 'host assert per frame; no saved GPU CB dump',
          'pendingDropNoLateCopyRecovery': True, 'installedLoaderAutoInitializeQualifiedInIsolatedHost': True,
          'actualGameLoaderTimingQualified': False, 'actualGameMainViewQualified': False,
          'packageSuitableForDefaultOffGameObservation': True, 'activeGameCopyQualified': False,
          'nr': 0, 'sr': 0, 'proof': proof, 'traceLogs': logs,
          'limits': 'Fixed captured layout, two exact shaders, immediate CPU View uploads, zero jitter. Unknown command lists invalidate caches. Guide texture semantics and actual game startup require default-off logs. Synthetic pending trial holds RGB frame0 while replaying real View698-700; no temporal image quality claim.'}
output.write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps({k: result[k] for k in ['ordinaryAndDebugEightFrameIdentity', 'defaultDisabledEightObservationsZeroCopies',
                                       'negativeCasesPerRun', 'drawAndSceneComparisons', 'pendingDropNoLateCopyRecovery',
                                       'installedLoaderAutoInitializeQualifiedInIsolatedHost', 'actualGameLoaderTimingQualified',
                                       'packageSuitableForDefaultOffGameObservation', 'nr', 'sr']}, indent=2))
