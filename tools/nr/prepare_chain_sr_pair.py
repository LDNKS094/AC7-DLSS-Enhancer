"""Build fresh SR pair from GPU-restored NR HDR and fresh GPU-normalized guides."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import math
import sys
import prepare_sr_diagnostic
from prepare_live_sr_pair import audited_frames


def main():
    source, experiment, output = map(Path, sys.argv[1:])
    if output.exists():
        raise ValueError('Refusing existing SR pair')
    if not (experiment / 'gpu-restore/complete.txt').exists():
        raise ValueError('GPU restoration incomplete')
    output.mkdir(parents=True)
    frames = []
    nr_contract = json.loads((experiment / 'nr-input/sequence-contract.json').read_text())
    audit = json.loads((source / 'audit.json').read_text())
    metadata = audited_frames(source, audit)
    if len(nr_contract['frames']) != len(metadata):
        raise ValueError('NR sequence frame count mismatch')
    for f in range(len(metadata)):
        guides = experiment / 'nr-input' / f'frame{f}'
        for name, sha in nr_contract['frames'][f]['hashes'].items():
            if hashlib.sha256((guides / name).read_bytes()).hexdigest() != sha:
                raise ValueError('NR input changed')
        if (guides / 'color.raw').read_bytes() != (experiment / 'gpu-restore' / f'frame{f}' / 'proxy.raw').read_bytes():
            raise ValueError('Restored proxy not equal actual NR input')
        dest = output / f'frame{f}'
        saved = sys.argv
        try:
            sys.argv = ['prepare_sr_diagnostic.py', str(experiment / 'gpu-restore' / f'frame{f}' / 'scene-restored.raw'),
                        str(guides), str(source / f'frame{f}-exposure.raw'), str(dest)]
            prepare_sr_diagnostic.main()
        finally:
            sys.argv = saved
        # Shared color converter writes its historical single-Reset label; replace derived contract with actual pair policy.
        contract = json.loads((dest / 'diagnostic-contract.json').read_text())
        contract.update(kind='REAL_NR_RESTORED_PAIR_SR_INPUT', reset=1 if f == 0 else 0,
                        limits='Actual adjacent NR outputs; same-size SR history execution only, P1 uncalibrated, zero jitter, not live interop or quality acceptance')
        (dest / 'diagnostic-contract.json').write_text(json.dumps(contract, indent=2))
        # Fixed8 uses the captured simulation delta, including sampling stalls.
        if len(metadata) == 8:
            dt = (metadata[f]['currentTimes'][0] - metadata[f]['previousTimes'][0]) * 1000
            if not math.isfinite(dt) or dt <= 0:
                raise ValueError('Invalid captured frame time')
            (dest / 'frame-time.bin').write_bytes(struct.pack('<f', dt))
        frames.append({'index': f, 'sourceFrameCandidate': metadata[f]['counter'],
                       'hashes': {name: hashlib.sha256((dest / name).read_bytes()).hexdigest() for name in ['color.raw', 'depth.raw', 'motion.raw', 'exposure.raw'] + (['frame-time.bin'] if len(metadata) == 8 else [])}})
    record = {'kind': 'REAL_NR_RESTORED_PAIR_SR_HISTORY', 'frames': frames, 'nrApplied': True,
              'source': str(source), 'preExposure': 1, 'exposureScale': 1, 'jitter': [0, 0],
              'limits': 'Fixed 2/8-frame NR->HDR->SR offline file/staging chain, not game writeback/live bridge/temporal quality acceptance'}
    (output / 'sequence-contract.json').write_text(json.dumps(record, indent=2))


if __name__ == '__main__':
    if len(sys.argv) != 4:
        raise ValueError('Usage: prepare_chain_sr_pair.py LIVE_PAIR CHAIN_EXPERIMENT NEW_SR_INPUT')
    main()
