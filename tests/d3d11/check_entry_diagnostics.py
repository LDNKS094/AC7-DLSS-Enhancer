"""Check the first real log and bounded diagnostics/full-view observation evidence."""
import json
import struct
from pathlib import Path
import sys

capture, root, output = map(Path, sys.argv[1:])
assert not output.exists()
first = [dict(item.split('=', 1) for item in line.split()) for line in (root / 'first-game.log').read_text().splitlines()]
assert [row['event'] for row in first] == ['bootstrap', 'installed', 'guide-shader', 'prewarp-shader']
assert all(row['enabled'] == row['applied'] == row['error'] == '0' for row in first)
assert first[-1]['prewarp'] == first[-1]['guides'] == '1'
assert int(first[1]['ms']) < int(first[2]['ms']) < int(first[3]['ms'])

def success(name):
    exit_status = json.loads((root / (name + '-exit.json')).read_text())
    assert exit_status['status'] == 0 and not exit_status['error']
    assert not (root / name / 'INCOMPLETE').exists()
    logs = list(root.glob(f'AC7NativeNeutral-{exit_status["pid"]}-*.log'))
    assert len(logs) == 1
    lines = [dict(item.split('=', 1) for item in line.split()) for line in logs[0].read_text().splitlines()]
    assert len(lines) <= 64
    notes = [row for row in lines if row['event'] == 'diagnostic']
    assert len(notes) <= 32 and len({(row['reason'], row['role']) for row in notes}) == len(notes)
    foreign = next(row for row in notes if row['reason'] == 'foreign-shape')
    assert foreign['ownerContext'] == foreign['deviceEqual'] == foreign['identityEqual'] == '0'
    assert foreign['contextType'] == '0'
    assert any(row['reason'] == 'owned-prewarp' and row['ownerContext'] == '1' for row in notes)
    assert any(row['reason'] == 'view-cache-miss' and row['cbBytes'] == '4096' for row in notes)
    return json.loads((root / name / 'checks.json').read_text()), lines

raw0 = (capture / 'frame0-scene.raw').read_bytes()
assert struct.unpack_from('<4I', raw0) == (5120, 1440, 26, 4)
negative = ['missing-guide', 'wrong-view-object', 'wrong-depth-object', 'changed-view-bytes',
            'invalid-view-replaces-cache', 'view-range-offset', 'gpu-copy-invalidates', 'gpu-region-invalidates',
            'gpu-region1-invalidates', 'unknown-command-list-invalidates', 'ambiguous-motion', 'stale-guide-counter']
for name, wide in [('wide-disabled', True), ('central-ordinary', False)]:
    checks, lines = success(name)
    assert checks['observedFrames'] == 8 and checks['applied'] == (0 if wide else 8)
    assert checks['negativeCases'] == 12 and checks['debugErrors'] == 0
    assert checks['syntheticFullViewport'] == wide and not checks['actualGame']
    assert checks['allScenesAndDrawsExact'] and checks['gpuViewMatchesObservedCpu'] and checks['boundedStoppedDrained']
    folder = root / name
    baseline = (folder / 'baseline-draw.raw').read_bytes()
    for file in negative:
        assert (folder / (file + '.raw')).read_bytes() == baseline
    records = [json.loads(line) for line in (folder / 'sequence.jsonl').read_text().splitlines()]
    for frame, record in enumerate(records):
        scene = (capture / f'frame{frame}-scene.raw').read_bytes()
        assert (folder / f'frame{frame}-scene.raw').read_bytes() == scene
        assert record['counter'] == record['token'] == 698 + frame
        expected = bytearray(baseline)
        for y in range(1440):
            start = 16 + (y * 5120 + (0 if wide else 1280)) * 4
            count = (5120 if wide else 2560) * 4
            expected[start:start + count] = scene[start:start + count]
        assert (folder / f'frame{frame}-draw.raw').read_bytes() == expected
    if wide:
        note = next(row for row in lines if row.get('reason') == 'full-view-observe-only')
        assert note['viewport'] == '0,0,5120,1440' and note['uploadRect'] == '0,0,5120,1440,5120,1440'
        assert note['decode'] == 'valid' and note['enabled'] == note['applied'] == '0'
        assert baseline == raw0
checks, lines = success('wide-guard')
assert checks['error'] == 13 and checks['applied'] == checks['debugErrors'] == 0
assert checks['originalDrawAndSceneExact'] and checks['stoppedDrained']
assert (root / 'wide-guard/wide-scene.raw').read_bytes() == raw0
assert (root / 'wide-guard/wide-active-refused.raw').read_bytes() == raw0
assert any(row.get('event') == 'error' and row.get('error') == '13' for row in lines)
checks, _ = success('central-pending')
assert checks['observedFrames'] == 3 and checks['applied'] == 2 and checks['pending'] == 1
assert checks['noLateSameFrameCopy'] and checks['recoveredCounter'] == 700 and checks['debugErrors'] == 0
assert (root / 'central-pending/pending-scene.raw').read_bytes() == raw0
result = {'realGameInitializeAndBothShaderCreationHooksConfirmed': True,
          'realGameViewAssociationQualified': False, 'firstLogNoViewRows': True,
          'firstLogDoesNotProveFinalUploadCount': True,
          'widescreenPatchChangesConstrainedRect': True, 'syntheticFullViewEightObservationsZeroCopies': True,
          'fullViewActivationRefusedBeforeCopy': True, 'centralEightFrameAndPendingRegression': True,
          'foreignContextOriginalDrawContinuedWithoutAdoption': True,
          'diagnosticsMaxUniqueNotes': 32, 'traceMaxLines': 64, 'models': 0,
          'next': 'User keeps widescreen patch, replaces only ASI after game exit, marker absent; inspect next bounded log. No automatic deployment or ZIP.'}
output.write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(result, indent=2))
