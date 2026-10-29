"""Check the independent neutral host's full-scene/crop artifacts and bounded controls."""
import hashlib
import json
from pathlib import Path
import struct
import sys

shader, source, run, report = map(Path, sys.argv[1:])
assert not report.exists(), 'Refusing previous report'
bytecode = shader.read_bytes()
assert len(bytecode) == 628 and hashlib.sha256(bytecode).hexdigest() == 'af1247e3a473861934047c3cabe849dae6577dc61189bc56ba225f3351294ec0'
scene = source.read_bytes()
assert len(scene) == 16 + 5120 * 1440 * 4 and struct.unpack_from('<4I', scene) == (5120, 1440, 26, 4)
status = json.loads((run.parent / (run.name + '-exit.json')).read_text())
assert status['status'] == 0 and not status['signal'] and not status['error']
assert not (run / 'INCOMPLETE').exists()
checks = json.loads((run / 'checks.json').read_text())
for name in ['defaultDisabled', 'exactShaderMatched', 'wrongHashRejected', 'unmatchedPreserved', 'sceneFullBitExact',
             'activeRegionBitExact', 'pipelineGetterSnapshotExact', 'hiddenCsUavCounterExact', 'originalComputeContinued', 'stopAndDrain']:
    assert checks[name] is True, name
assert checks['bindingRefusals'] == 6 and checks['gatedPendingChecks'] == 8
assert checks['debugErrors'] == checks['modelEvaluations'] == 0 and checks['gameHookInstalled'] is False
assert (run / 'neutral-scene.raw').read_bytes() == scene, 'Neutral scene differs'
cropped = b''.join(scene[16 + (y * 5120 + 1280) * 4:16 + (y * 5120 + 3840) * 4] for y in range(1440))
actual = (run / 'captured-region.raw').read_bytes()
assert struct.unpack_from('<4I', actual) == (2560, 1440, 26, 4) and actual[16:] == cropped, 'Active crop differs'
assert 'PASS neutral host fullScene/activeRegion/state/UAVcounter/refusals/pending8/stop/debug0; model0 hook0' in (run.parent / (run.name + '-process.log')).read_text()
result = {'kind': 'NEUTRAL_ENTRY_INDEPENDENT_ARTIFACT_CHECK', 'sourceSha256': hashlib.sha256(scene).hexdigest(),
          'shaderSha256': hashlib.sha256(bytecode).hexdigest(), 'fullSceneExact': True, 'activeCropExact': True,
          'checks': checks, 'actualGameDrawExercised': False, 'installedHook': False,
          'limits': 'Synthetic D11 FL11.0 host uses captured shader and actual scene fixture. Matching checks shape/object binding only; View contents/continuous frame identity and guide semantic correlation are not qualified. Getter snapshot plus CS hidden counter/continued dispatch; nonempty SO offsets/hidden counters and shader class instances not exercised. No ASI load, hooks, game deployment, NGX, model, FG or performance acceptance.'}
report.write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps({'fullSceneExact': True, 'activeCropExact': True, 'stateSnapshotExact': True, 'hiddenCounterAndDispatch': True,
                  'bindingRefusals': 6, 'pendingChecks': 8, 'debugErrors': 0, 'modelEvaluations': 0, 'hookInstalled': False}, indent=2))
