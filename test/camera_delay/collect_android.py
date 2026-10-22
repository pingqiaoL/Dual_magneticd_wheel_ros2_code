#!/usr/bin/env python3
"""Read existing production diagnostics; no APK/source changes or log clearing."""
import argparse
import json
import subprocess
import time
from datetime import datetime, timezone, timedelta
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--device', default='192.168.74.178:5555')
    parser.add_argument('--seconds', type=int, default=75)
    parser.add_argument('--restart', action='store_true')
    args = parser.parse_args()
    adb = Path.home() / 'Android/Sdk/platform-tools/adb'
    output = Path(__file__).resolve().parent / 'results' / datetime.now(timezone(timedelta(hours=8))).strftime('android_%Y%m%d_%H%M%S')
    output.mkdir(parents=True)

    def command(*parts):
        result = subprocess.run([str(adb), '-s', args.device, *parts], capture_output=True, timeout=20)
        if result.returncode:
            raise RuntimeError(result.stderr.decode(errors='replace'))
        return result.stdout

    def snapshot(name):
        (output / (name + '.csv')).write_bytes(command('exec-out', 'run-as', 'com.climbwheel.mk32', 'cat', 'files/video_diagnostics.csv'))

    metadata = dict(device=args.device, seconds=args.seconds, restart=args.restart)
    metadata['start_uptime'] = command('shell', 'cat', '/proc/uptime').decode().strip()
    metadata['start_device_epoch_s'] = command('shell', 'date', '+%s').decode().strip()
    snapshot('before')
    if args.restart:
        command('shell', 'am', 'force-stop', 'com.climbwheel.mk32')
        command('shell', 'am', 'start', '-n', 'com.climbwheel.mk32/.MainActivity')
    print('RESULTS=' + str(output), flush=True)
    try:
        time.sleep(args.seconds)
    finally:
        metadata['end_uptime'] = command('shell', 'cat', '/proc/uptime').decode().strip()
        metadata['end_device_epoch_s'] = command('shell', 'date', '+%s').decode().strip()
        snapshot('after')
        for name in ('video_diagnostics.previous.csv',):
            result = subprocess.run([str(adb), '-s', args.device, 'exec-out', 'run-as', 'com.climbwheel.mk32', 'cat', 'files/' + name], capture_output=True, timeout=20)
            if result.returncode == 0:
                (output / name).write_bytes(result.stdout)
        (output / 'logcat.txt').write_bytes(command('logcat', '-d', '-v', 'epoch', '-s', 'Mk32Video:I', 'Mk32VideoDiag:I', 'RtspClient:V', 'RtpH264Reader:W', 'MediaCodecVideoRenderer:W', '*:S'))
        (output / 'package.txt').write_bytes(command('shell', 'dumpsys', 'package', 'com.climbwheel.mk32'))
        (output / 'network.txt').write_bytes(command('shell', 'ip', 'address'))
        (output / 'metadata.json').write_text(json.dumps(metadata, indent=2))
        print('COMPLETE=' + str(output), flush=True)


if __name__ == '__main__':
    main()
