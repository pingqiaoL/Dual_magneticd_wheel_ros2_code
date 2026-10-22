#!/usr/bin/env python3
"""Analyze bounded Android windows and Nano counter deltas without mixing clocks."""
import argparse
import csv
import io
import json
import math
import re
import statistics
from pathlib import Path


def distribution(values):
    values = sorted(values)
    if not values:
        return {'count': 0}
    return dict(count=len(values), minimum=values[0], median=statistics.median(values),
                mean=statistics.mean(values), p95=values[math.ceil(.95 * len(values)) - 1], maximum=values[-1])


def analyze_android(directory, warmup_seconds=15):
    metadata = json.loads((directory / 'metadata.json').read_text())
    start = float(metadata['start_uptime'].split()[0]) * 1000
    end = float(metadata['end_uptime'].split()[0]) * 1000
    rows = []
    seen = set()
    # Uptime repeats after reboots; reject every row already present before sampling.
    prior = {(row['wall_time_ms'], row['elapsed_realtime_ms'], row['event'], row['details'])
             for row in csv.DictReader(io.StringIO((directory / 'before.csv').read_text(encoding='utf-8')))}
    names = ['after.csv']
    if not (directory / 'after.csv').read_bytes().startswith((directory / 'before.csv').read_bytes()):
        names.append('video_diagnostics.previous.csv')
    for name in names:
        path = directory / name
        if not path.exists():
            continue
        for row in csv.DictReader(io.StringIO(path.read_text(encoding='utf-8'))):
            elapsed = int(row['elapsed_realtime_ms'])
            identity = (row['wall_time_ms'], row['elapsed_realtime_ms'], row['event'], row['details'])
            if start <= elapsed <= end and identity not in seen and identity not in prior:
                seen.add(identity)
                row['elapsed_ms'] = elapsed
                row['fields'] = dict(re.findall(r'(\w+)=([^\s]+)', row['details']))
                rows.append(row)
    rows.sort(key=lambda row: row['elapsed_ms'])
    stats = [row for row in rows if row['event'] == 'STATS']
    if not stats:
        raise RuntimeError('No fresh STATS; do not substitute old playback logs')
    # Separate startup from settled playback, retaining both summaries.
    steady = [row for row in stats if row['elapsed_ms'] >= start + warmup_seconds * 1000]
    def metric(name, source=steady, scale=1):
        values = [float(row['fields'][name]) * scale for row in source if name in row['fields']
                  and float(row['fields'][name]) > -1e12]
        return distribution(values)
    rendered_delta, elapsed_ms, dropped_delta = 0, 0, 0
    for previous, current in zip(steady, steady[1:]):
        duration = current['elapsed_ms'] - previous['elapsed_ms']
        delta = int(current['fields']['rendered']) - int(previous['fields']['rendered'])
        if 0 < duration < 3000 and delta >= 0:
            rendered_delta += delta
            elapsed_ms += duration
            dropped_delta += max(0, int(current['fields']['dropped']) - int(previous['fields']['dropped']))
    third = max(1, len(steady) // 3)
    buffering_started = None
    rebuffer_durations = []
    steady_rebuffers = 0
    for row in rows:
        if row['event'] == 'BUFFERING' and row['fields'].get('prepared') == 'true':
            if buffering_started is None:
                buffering_started = row['elapsed_ms']
                if buffering_started >= start + warmup_seconds * 1000:
                    steady_rebuffers += 1
        elif row['event'] == 'READY' and buffering_started is not None:
            rebuffer_durations.append(row['elapsed_ms'] - buffering_started)
            buffering_started = None
    result = dict(kind='android', source=str(directory), warmup_seconds=warmup_seconds, window_start_ms=start, window_end_ms=end,
                  fresh_events=len(rows), fresh_stats=len(stats), steady_stats=len(steady),
                  buffered_all_ms=metric('buffered_ms', stats), buffered_steady_ms=metric('buffered_ms'),
                  first_third_buffer_ms=metric('buffered_ms', steady[:third]),
                  last_third_buffer_ms=metric('buffered_ms', steady[-third:]),
                  release_lead_ms=metric('release_lead_us', scale=.001),
                  frame_age_ms=metric('frame_age_ms'), speed=metric('speed'),
                  rendered_fps=rendered_delta * 1000 / elapsed_ms if elapsed_ms else None,
                  rendered_frame_delta=rendered_delta, observed_span_ms=elapsed_ms,
                  decoder_dropped_delta=dropped_delta,
                  rebuffer_duration_ms=distribution(rebuffer_durations),
                  rebuffer_total_ms=sum(rebuffer_durations), steady_rebuffer_events=steady_rebuffers,
                  startup=[dict(event=row['event'], **row['fields']) for row in rows if row['event'] in ('FIRST_FRAME', 'READY', 'DECODER', 'OPEN')],
                  exceptional_events=[row for row in rows if row['event'] in ('BACKLOG', 'FRAME_STALL', 'PLAYER_ERROR', 'STALL_TIMEOUT', 'CODEC_ERROR', 'NETWORK_CHANGED')],
                  buffering_events=[row['fields'] for row in rows if row['event'] == 'BUFFERING'])
    (directory / 'summary.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))
    return result


def analyze_nano(directory):
    rows = list(csv.DictReader(io.StringIO((directory / 'resources.csv').read_text(encoding='utf-8'))))
    cpu, tx_mbps = [], []
    for previous, current in zip(rows, rows[1:]):
        total = int(current['cpu_total_ticks']) - int(previous['cpu_total_ticks'])
        idle = int(current['cpu_idle_ticks']) - int(previous['cpu_idle_ticks'])
        if total > 0:
            cpu.append(100 * (total - idle) / total)
        duration = float(current['monotonic_s']) - float(previous['monotonic_s'])
        tx_mbps.append((int(current['tx_bytes']) - int(previous['tx_bytes'])) * 8 / duration / 1e6)
    result = dict(kind='nano', samples=len(rows), cpu_percent_all_cores=distribution(cpu),
                  eth0_tx_mbps_all_traffic=distribution(tx_mbps),
                  minimum_mem_available_kb=min(int(row['mem_available_kb']) for row in rows),
                  network_counter_deltas={name: int(rows[-1][name]) - int(rows[0][name]) for name in ('rx_errors', 'tx_errors', 'rx_dropped', 'tx_dropped')})
    def counters(name):
        text = (directory / name).read_text()
        return {key: int(value) for key, value in re.findall(r"key: (\w+)\s+value: '([0-9]+)'", text)}
    before = counters('before_video_status.txt')
    after = counters('after_video_status.txt')
    result['video_counter_deltas'] = {key: after[key] - before[key] for key in before if key in after and key != 'client_count'}
    result['status_note'] = 'Status snapshots include setup/teardown sampling overhead; no exact status FPS inferred.'
    (directory / 'summary.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('kind', choices=('android', 'nano'))
    parser.add_argument('directory', type=Path)
    parser.add_argument('--warmup', type=float, default=15)
    args = parser.parse_args()
    if args.kind == 'android':
        analyze_android(args.directory, args.warmup)
    else:
        analyze_nano(args.directory)
