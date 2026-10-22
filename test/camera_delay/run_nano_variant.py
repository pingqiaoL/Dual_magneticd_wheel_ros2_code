#!/usr/bin/env python3
"""Temporarily run an isolated sender candidate and retain original measurement launch."""
import argparse
import json
import os
import signal
import sys
import time
from datetime import datetime, timezone, timedelta
from pathlib import Path

sys.path.insert(0, '/workspace/test/camera_delay_test')
from run_matrix import processes, start_logged, stop_tree

parser = argparse.ArgumentParser()
parser.add_argument('--seconds', type=int, default=120)
parser.add_argument('--pause-tests', action='store_true')
args = parser.parse_args()
suite = Path(__file__).resolve().parent
runtime = suite / 'optimization_workspaces/nano_latest'
output = suite / 'results' / datetime.now(timezone(timedelta(hours=8))).strftime('nano_candidate_%Y%m%d_%H%M%S')
current = processes()
originals = [(pid, command) for pid, command in current.items() if '/workspace/activate/mix_module/camera_thickness.sh' in command]
measurements = [command for command in current.values() if 'measurement_manager' in command and 'thickness.launch.xml' in command and 'launch' in command]
if len(originals) != 1 or len(measurements) != 1:
    raise RuntimeError('Expected one original combined entrypoint and measurement launch')
output.mkdir(parents=True)
(output / 'original_commands.json').write_text(json.dumps(dict(combined=originals[0][1], measurement=measurements[0]), indent=2))
children = []
def interrupted(signum, frame):
    raise KeyboardInterrupt()
signal.signal(signal.SIGTERM, interrupted)
signal.signal(signal.SIGINT, interrupted)
try:
    stop_tree(originals[0][0])
    children.append(start_logged(measurements[0], output / 'measurement.log', os.environ.copy()))
    children.append(start_logged(['bash', '-c', f'source {runtime}/install/local_setup.bash && exec ros2 launch video_streamer_transmission video.launch.xml'], output / 'video.log', os.environ.copy()))
    deadline = time.monotonic() + 20
    while 'RTSP :8554/live' not in (output / 'video.log').read_text():
        if children[-1].poll() is not None or time.monotonic() > deadline:
            raise RuntimeError((output / 'video.log').read_text())
        time.sleep(.1)
    libraries = []
    candidate_pid = None
    for process in Path('/proc').iterdir():
        if not process.name.isdigit():
            continue
        try:
            command = (process / 'cmdline').read_bytes().split(b'\0')
            if b'__node:=video_container' in command:
                libraries += [line for line in (process / 'maps').read_text().splitlines() if 'libvideo_' in line]
                candidate_pid = int(process.name)
        except (FileNotFoundError, PermissionError, ProcessLookupError):
            pass
    (output / 'loaded_libraries.json').write_text(json.dumps(libraries, indent=2))
    if not any(str(runtime) in line for line in libraries):
        raise RuntimeError('Candidate library was not loaded; do not label this an A/B test')
    print('READY=' + str(output), flush=True)
    started = time.monotonic()
    pauses = [(40, .25), (65, .8)] if args.pause_tests else []
    pause_events = []
    for after_seconds, duration_seconds in pauses:
        if after_seconds >= args.seconds:
            continue
        time.sleep(max(0, started + after_seconds - time.monotonic()))
        event = dict(start_monotonic_ns=time.monotonic_ns(), duration_seconds=duration_seconds)
        os.kill(candidate_pid, signal.SIGSTOP)
        try:
            time.sleep(duration_seconds)
        finally:
            os.kill(candidate_pid, signal.SIGCONT)
        event['end_monotonic_ns'] = time.monotonic_ns()
        pause_events.append(event)
        (output / 'pause_events.json').write_text(json.dumps(pause_events, indent=2))
        print('PAUSE_TEST=' + str(duration_seconds), flush=True)
    time.sleep(max(0, started + args.seconds - time.monotonic()))
finally:
    for child in reversed(children):
        if child.poll() is None:
            stop_tree(child.pid)
            child.wait(timeout=20)
    restored = start_logged(['/usr/local/bin/robot-entrypoint'] + originals[0][1], output / 'restored.log', os.environ.copy())
    print('RESTORED=' + str(restored.pid), flush=True)
