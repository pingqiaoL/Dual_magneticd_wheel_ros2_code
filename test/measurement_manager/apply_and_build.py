"""Apply the approved manager simplification and restore the running launch."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
from datetime import datetime
from zoneinfo import ZoneInfo

sys.path.insert(0, '/workspace/test/camera_delay_test')
from run_matrix import processes, start_logged, stop_tree

root = Path('/workspace')
os.chdir(root)
suite = root / 'test/measurement_manager'
suite.mkdir(exist_ok=True)
(suite / 'COLCON_IGNORE').touch()
(suite / '.gitignore').write_text('results/\n')
output = suite / 'results' / datetime.now(ZoneInfo('Asia/Shanghai')).strftime('change_%Y%m%d_%H%M%S')
output.mkdir(parents=True)
originals = [(pid, args) for pid, args in processes().items()
             if args and args[0] == 'bash' and len(args) > 1
             and args[1] == '/workspace/activate/mix_module/camera_thickness.sh']
if len(originals) != 1:
    raise RuntimeError('Expected exactly one known running combined launch')
(output / 'original_command.json').write_text(json.dumps(originals[0][1]))
with tarfile.open('/tmp/measurement_changes.tar.gz') as archive:
    members = archive.getnames()
removed = 'src/robot_interfaces/msg/MeasurementControl.msg'
missing = [name for name in members if not (root / name).exists()]
with tarfile.open(output / 'source_backup.tar.gz', 'w:gz') as backup:
    for name in members + [removed]:
        if (root / name).exists():
            backup.add(root / name, arcname=name)
(output / 'new_files.json').write_text(json.dumps(missing))
build_command = ['colcon', 'build', '--base-paths', 'src', '--packages-select',
                 'robot_interfaces', 'thickness_usb_driver', 'measurement_manager',
                 'telemetry_bridge', '--cmake-clean-first', '--event-handlers', 'console_direct+',
                 '--cmake-args', '-DBUILD_TESTING=ON']
try:
    stop_tree(originals[0][0])
    with tarfile.open('/tmp/measurement_changes.tar.gz') as archive:
        archive.extractall(root, filter='data')
    (root / removed).unlink()
    # Retire all generated old-interface artifacts to avoid leaving an importable stale type.
    for parent in ('build', 'install'):
        path = root / parent / 'robot_interfaces'
        if path.exists():
            if path.resolve().parent != (root / parent).resolve():
                raise RuntimeError('Unexpected interface artifact path')
            shutil.move(str(path), output / ('previous_' + parent + '_robot_interfaces'))
    with (output / 'build.log').open('w') as log:
        subprocess.run(build_command, stdout=log, stderr=subprocess.STDOUT,
                       timeout=300, check=True)
    (output / 'BUILD_PASSED').touch()
    print('BUILD_PASSED=' + str(output), flush=True)
except BaseException:
    with tarfile.open(output / 'source_backup.tar.gz') as archive:
        archive.extractall(root, filter='data')
    for name in missing:
        (root / name).unlink(missing_ok=True)
    for parent in ('build', 'install'):
        previous = output / ('previous_' + parent + '_robot_interfaces')
        if previous.exists():
            current = root / parent / 'robot_interfaces'
            if current.exists():
                shutil.move(str(current), output / ('failed_' + parent + '_robot_interfaces'))
            shutil.move(str(previous), current)
    with (output / 'rollback_build.log').open('w') as log:
        subprocess.run(build_command, stdout=log, stderr=subprocess.STDOUT,
                       timeout=300, check=True)
    raise
finally:
    restored = start_logged(['/usr/local/bin/robot-entrypoint'] + originals[0][1],
                            output / 'restored.log', os.environ.copy())
    print('RESTORED_PID=' + str(restored.pid), flush=True)
