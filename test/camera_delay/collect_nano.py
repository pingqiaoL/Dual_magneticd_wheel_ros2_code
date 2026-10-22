#!/usr/bin/env python3
"""Read Nano resource counters and existing ROS status without changing runtime."""
import argparse
import csv
import json
import subprocess
import time
from datetime import datetime, timezone, timedelta
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--seconds', type=int, default=75)
    args = parser.parse_args()
    output = Path(__file__).resolve().parent / 'results' / datetime.now(timezone(timedelta(hours=8))).strftime('nano_%Y%m%d_%H%M%S')
    output.mkdir(parents=True)
    print('RESULTS=' + str(output), flush=True)
    def run(name, command):
        try:
            result = subprocess.run(command, capture_output=True, timeout=15)
            (output / name).write_bytes(result.stdout + result.stderr)
        except (OSError, subprocess.TimeoutExpired) as error:
            (output / name).write_text('Unavailable: ' + str(error))
    def status(prefix):
        for topic in ('/camera/status', '/video/status'):
            run(prefix + topic.replace('/', '_') + '.txt', ['ros2', 'topic', 'echo', topic, '--once'])
    status('before')
    for node in ('/camera_capture_node', '/video_streamer_node'):
        run(node[1:] + '_params.yaml', ['ros2', 'param', 'dump', node])
    run('processes.txt', ['ps', '-eo', 'pid,pcpu,pmem,args'])
    run('sockets.txt', ['ss', '-tunp'])
    started = time.monotonic()
    with (output / 'resources.csv').open('w', newline='') as stream:
        writer = csv.writer(stream)
        writer.writerow(['monotonic_s', 'cpu_total_ticks', 'cpu_idle_ticks', 'mem_available_kb', 'tx_bytes', 'rx_bytes', 'tx_dropped', 'rx_dropped', 'rx_errors', 'tx_errors'])
        while time.monotonic() - started < args.seconds:
            ticks = list(map(int, Path('/proc/stat').read_text().splitlines()[0].split()[1:]))
            # guest/guest_nice are already included in user/nice.
            memory = dict(line.split(':', 1) for line in Path('/proc/meminfo').read_text().splitlines())
            network = Path('/sys/class/net/eth0/statistics')
            values = [int((network / name).read_text()) for name in ('tx_bytes', 'rx_bytes', 'tx_dropped', 'rx_dropped', 'rx_errors', 'tx_errors')]
            writer.writerow([time.monotonic(), sum(ticks[:8]), ticks[3] + ticks[4], int(memory['MemAvailable'].split()[0]), *values])
            stream.flush()
            time.sleep(1)
    status('after')
    (output / 'metadata.json').write_text(json.dumps(dict(seconds=args.seconds, ros_domain_id=42), indent=2))
    print('COMPLETE=' + str(output), flush=True)


if __name__ == '__main__':
    main()
