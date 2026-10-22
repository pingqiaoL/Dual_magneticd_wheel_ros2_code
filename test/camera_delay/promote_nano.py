#!/usr/bin/env python3
"""Install the validated sender changes while its known entrypoint is stopped."""
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, '/workspace/test/camera_delay_test')
from run_matrix import processes, start_logged, stop_tree

root = Path('/workspace')
suite = Path(__file__).resolve().parent
variant = suite / 'optimization_workspaces/nano_latest'
package = Path('src/video_streamer_transmission')
manifest = json.loads((variant / 'baseline_manifest.json').read_text())
for relative, digest in manifest.items():
    if hashlib.sha256((root / package / relative).read_bytes()).hexdigest() != digest:
        raise RuntimeError('Concurrent source change: ' + relative)
originals = [(pid, command) for pid, command in processes().items() if '/workspace/activate/mix_module/camera_thickness.sh' in command]
if len(originals) != 1:
    raise RuntimeError('Expected one original combined entrypoint')
backup = suite / 'results/optimization_backup/nano_sources'
for relative in manifest:
    destination = backup / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(root / package / relative, destination)
command = ['colcon', 'build', '--base-paths', 'src', '--packages-select', 'video_streamer_transmission', '--cmake-clean-first', '--cmake-args', '-DBUILD_TESTING=OFF', '-DCMAKE_BUILD_TYPE=']
try:
    stop_tree(originals[0][0])
    for relative in manifest:
        shutil.copy2(variant / package / relative, root / package / relative)
    with (suite / 'results/production_nano_build.log').open('w') as stream:
        result = subprocess.run(command, cwd=root, stdout=stream, stderr=subprocess.STDOUT, timeout=180)
    if result.returncode:
        raise RuntimeError('Production build failed; see production_nano_build.log')
    (suite / 'results/selected_nano.json').write_text(json.dumps(dict(
        raw_queue_frames=1, appsrc_frames=1, max_frame_age_ms=150,
        changed_files=list(manifest),
        source_hashes={relative: hashlib.sha256((root / package / relative).read_bytes()).hexdigest() for relative in manifest}), indent=2))
except BaseException:
    for relative in manifest:
        shutil.copy2(backup / relative, root / package / relative)
    with (suite / 'results/rollback_nano_build.log').open('w') as stream:
        subprocess.run(command, cwd=root, stdout=stream, stderr=subprocess.STDOUT, timeout=180, check=True)
    raise
finally:
    restored = start_logged(['/usr/local/bin/robot-entrypoint'] + originals[0][1], suite / 'results/production_restored.log', os.environ.copy())
    print('RESTORED=' + str(restored.pid), flush=True)
