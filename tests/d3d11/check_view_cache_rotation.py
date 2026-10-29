"""Verify capacity failure reproduction, bounded rotation and unchanged GPU outputs."""
import json
from pathlib import Path
import struct
import sys

capture, root, output = map(Path, sys.argv[1:])
assert not output.exists()
third = [dict(item.split('=', 1) for item in line.split()) for line in (root / 'third-game.log').read_text().splitlines()]
owner = next(row for row in third if row.get('reason') == 'owned-guide')
assert owner['module'] == 'd3d11.dll' and owner['ownerContext'] == owner['deviceEqual'] == owner['identityEqual'] == '1'
assert third[-1]['error'] == '10' and third[-1]['uploads'] == '78' and third[-1]['prewarp'] == '0'
assert all(row['enabled'] == row['applied'] == '0' for row in third)
old = json.loads((root / 'old-rotation-failure/rotation-check.json').read_text())
assert old['distinctUploadedBuffers'] == 32 and old['error'] == 10 and old['stopped'] == 1 and old['uploads'] == 16
assert (root / 'old-rotation-failure/INCOMPLETE').exists()
assert json.loads((root / 'old-rotation-failure-exit.json').read_text())['status'] == 1
proof = []
for name in ['wide-disabled', 'central-ordinary', 'central-pending', 'wide-guard']:
    status = json.loads((root / (name + '-exit.json')).read_text())
    assert status['status'] == 0 and not status['error'] and not (root / name / 'INCOMPLETE').exists()
    folder = root / name
    rotation = json.loads((folder / 'rotation-check.json').read_text())
    assert rotation['distinctUploadedBuffers'] == rotation['uploads'] == 32 and rotation['error'] == rotation['stopped'] == 0
    mapped = json.loads((folder / 'mapped-rotation-check.json').read_text())
    assert mapped['simultaneousMappings'] == 17 and mapped['observedOnFirstUnmapBatch'] == 16
    assert mapped['refreshedOldestRecovered'] and mapped['boundedCacheSelfCheck'] and mapped['error'] == 0
    logs = list(root.glob(f'AC7NativeNeutral-{status["pid"]}-*.log'))
    assert len(logs) == 1
    lines = [dict(item.split('=', 1) for item in line.split()) for line in logs[0].read_text().splitlines()]
    assert len(lines) <= 64
    records = [line for line in lines if 'cacheEntries' in line]
    assert records and max(int(line['cacheEntries']) for line in records) <= 16
    assert max(int(line['mapped']) for line in records) <= 16
    assert max(int(line['cacheEvictions']) for line in records) >= 16
    assert max(int(line['mapEvictions']) for line in records) == 1
    checks = json.loads((folder / 'checks.json').read_text())
    assert checks['debugErrors'] == 0 and checks['modelEvaluations'] == 0
    base = (folder / 'baseline-draw.raw').read_bytes()
    assert struct.unpack_from('<4I', base) == (5120, 1440, 26, 4)
    if name in ['wide-disabled', 'central-ordinary']:
        wide = name == 'wide-disabled'
        assert checks['observedFrames'] == 8 and checks['applied'] == (0 if wide else 8)
        assert checks['negativeCases'] == 12 and checks['allScenesAndDrawsExact'] and checks['gpuViewMatchesObservedCpu']
        sequence = [json.loads(line) for line in (folder / 'sequence.jsonl').read_text().splitlines()]
        assert len(sequence) == 8
        for frame, row in enumerate(sequence):
            assert row['counter'] == row['token'] == 698 + frame
            scene = (capture / f'frame{frame}-scene.raw').read_bytes()
            assert (folder / f'frame{frame}-scene.raw').read_bytes() == scene
            expected = bytearray(base)
            for y in range(1440):
                start = 16 + (y * 5120 + (0 if wide else 1280)) * 4
                count = (5120 if wide else 2560) * 4
                expected[start:start + count] = scene[start:start + count]
            assert (folder / f'frame{frame}-draw.raw').read_bytes() == expected
    elif name == 'central-pending':
        assert checks['observedFrames'] == 3 and checks['applied'] == 2 and checks['pending'] == 1
        assert checks['noLateSameFrameCopy'] and checks['recoveredCounter'] == 700
        assert (folder / 'pending-scene.raw').read_bytes() == (capture / 'frame0-scene.raw').read_bytes()
    else:
        assert checks['error'] == 13 and checks['applied'] == 0 and checks['originalDrawAndSceneExact']
        assert (folder / 'wide-scene.raw').read_bytes() == (capture / 'frame0-scene.raw').read_bytes()
    proof.append({'run': name, 'log': logs[0].name, 'lines': len(lines), 'cacheCapacity': 16, 'mapCapacity': 16})
result = {'nativeOwnerNowConfirmedInActualGame': True, 'actualGameError10Observed': True,
          'oldLifetimeCapacityBugReproducedWithSameAsi': True,
          'actualOldError10PreciseExceptionNotRecorded': True,
          'boundedRecentUploadEvictionPassed': True, 'boundedMappedTrackingDropAndRefreshPassed': True,
          'evictedOrInvalidIdentityNotReused': True, 'centralAndWideRegressionPassed': True,
          'pendingAndWideActivationRefusalPassed': True, 'actualGameAfterFixQualified': False,
          'models': 0, 'proof': proof}
output.write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(result, indent=2))
