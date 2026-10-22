#!/usr/bin/env python3
"""Join exact frame identifiers and monotonic timestamps, then summarize ms."""

import argparse
import csv
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path


def distribution(values):
    values = sorted(values)
    if not values:
        return dict(count=0)
    return dict(count=len(values), mean=statistics.mean(values), median=statistics.median(values),
                p95=values[max(0, math.ceil(len(values) * 0.95) - 1)], maximum=values[-1])


def analyze(directory):
    metadata = json.loads((directory / 'metadata.json').read_text())
    start, end = metadata['measurement_start_ns'], metadata['measurement_end_ns']
    stages = defaultdict(dict)
    links = defaultdict(dict)
    malformed_rows = 0
    for path in list(directory.glob('trace_*.csv')) + [directory / 'viewer.csv']:
        if not path.exists():
            continue
        with path.open() as stream:
            for row in csv.reader(stream):
                try:
                    timestamp, stage, frame_id, related_id = row
                    stages[stage][int(frame_id)] = int(timestamp)
                    links[stage][int(frame_id)] = int(related_id)
                except (ValueError, TypeError):
                    malformed_rows += 1
    metrics = defaultdict(list)
    joined = []

    def add(name, first, last, reference):
        if first is not None and last is not None and start <= reference < end:
            if last >= first:
                metrics[name].append((last - first) / 1e6)
            else:
                raise RuntimeError(f'Negative stage duration: {name}')

    encoding_match = dict(input_count=len(stages['encoder_input']), output_count=len(stages['encoded']))
    encoded_by_input = {}
    if stages['encoder_input'] and stages['encoded']:
        # x264 changes PTS origin. No B frames; validate a constant offset by exact ID matching.
        offset = min(stages['encoded']) - min(stages['encoder_input'])
        encoded_by_input = {frame_id - offset: timestamp for frame_id, timestamp in stages['encoded'].items()}
        matched = set(stages['encoder_input']) & set(encoded_by_input)
        ratio = len(matched) / len(stages['encoded'])
        encoding_match.update(pts_offset_ns=offset, matched_count=len(matched), match_ratio=ratio)
        if ratio < 0.95:
            raise RuntimeError(f'Cannot reliably pair encoded frames: {encoding_match}')

    for ros_id, published in stages['publish'].items():
        camera_id = links['publish'][ros_id]
        source = stages['source'].get(camera_id)
        sample = stages['sample'].get(camera_id)
        copied = stages['copy_complete'].get(camera_id)
        received = stages['receive'].get(ros_id)
        display_start = stages['display_start'].get(ros_id)
        converted = stages['convert_end'].get(ros_id)
        drawn = stages['draw_end'].get(ros_id)
        gui_end = stages['gui_end'].get(ros_id)
        video_receive = stages['video_receive'].get(ros_id)
        push = stages['push'].get(ros_id)
        input_pts = links['push'].get(ros_id)
        encoder_input = stages['encoder_input'].get(input_pts)
        encoded = encoded_by_input.get(input_pts)
        for name, first, last in [
            ('capture_pipeline_ms', source, sample), ('copy_and_status_ms', sample, copied),
            ('publish_prepare_ms', copied, published), ('ros_to_viewer_ms', published, received),
            ('viewer_wait_ms', received, display_start), ('cv_convert_ms', display_start, converted),
            ('cv_overlay_ms', converted, drawn), ('cv_gui_call_ms', drawn, gui_end),
            ('source_to_gui_return_ms', source, gui_end),
            ('ros_to_video_ms', published, video_receive), ('video_wait_and_copy_ms', video_receive, push),
            ('encoder_prequeue_ms', push, encoder_input), ('encode_and_parse_ms', encoder_input, encoded),
            ('video_callback_to_encoded_ms', video_receive, encoded),
            ('source_to_encoded_ms', source, encoded),
        ]:
            add(name, first, last, published)
        if start <= published < end:
            joined.append([ros_id, camera_id, source, sample, copied, published, received,
                           display_start, converted, drawn, gui_end, video_receive, push,
                           encoder_input, encoded])
    with (directory / 'frames.csv').open('w', newline='') as stream:
        writer = csv.writer(stream)
        writer.writerow(['ros_frame_id', 'camera_pts', 'source_ns', 'sample_ns', 'copy_complete_ns',
                         'publish_ns', 'receive_ns', 'display_start_ns', 'convert_end_ns', 'draw_end_ns',
                         'gui_end_ns', 'video_receive_ns', 'push_ns', 'encoder_input_ns', 'encoded_ns'])
        writer.writerows(joined)
    fps = {stage: sum(start <= timestamp < end for timestamp in timestamps.values()) / metadata['seconds']
           for stage, timestamps in stages.items()}
    with (directory / 'resources.csv').open() as stream:
        resources = list(csv.DictReader(stream))
    cpu = []
    for previous, current in zip(resources, resources[1:]):
        if start <= int(current['monotonic_ns']) < end:
            total = int(current['cpu_total_ticks']) - int(previous['cpu_total_ticks'])
            idle = int(current['cpu_idle_ticks']) - int(previous['cpu_idle_ticks'])
            if total:
                cpu.append(100 * (total - idle) / total)
    result = dict(metadata=metadata, metrics={name: distribution(values) for name, values in metrics.items()},
                  fps=fps, encoding_match=encoding_match, malformed_rows=malformed_rows,
                  cpu_total_percent=distribution(cpu))
    (directory / 'summary.json').write_text(json.dumps(result, indent=2))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('run_directory', type=Path)
    arguments = parser.parse_args()
    if not (arguments.run_directory / 'completed').exists():
        raise RuntimeError('Incomplete test matrix; do not report partial samples as a complete run')
    results = {}
    for directory in sorted(arguments.run_directory.iterdir()):
        if (directory.name in ['A_headless', 'B_legacy_cv', 'C_latest_cv', 'D_video', 'E_full']
                and (directory / 'completed').exists()):
            results[directory.name] = analyze(directory)
    (arguments.run_directory / 'summary.json').write_text(json.dumps(results, indent=2))
    lines = ['# Nano 摄像头延迟实测', '', '单位：ms。表内为中位数 / P95。', '',
             '| 环节 | ' + ' | '.join(results) + ' |',
             '|---|' + '---|' * len(results)]
    metric_names = list(dict.fromkeys(name for result in results.values() for name in result['metrics']))
    for name in metric_names:
        cells = []
        for result in results.values():
            item = result['metrics'].get(name)
            cells.append(f"{item['median']:.2f} / {item['p95']:.2f}" if item and item['count'] else '—')
        lines.append('| ' + name + ' | ' + ' | '.join(cells) + ' |')
    lines += ['', '| 指标 | ' + ' | '.join(results) + ' |', '|---|' + '---|' * len(results)]
    for name in ['publish', 'receive', 'gui_end', 'encoded']:
        lines.append('| ' + name + ' FPS | ' + ' | '.join(f"{result['fps'].get(name, 0):.2f}" for result in results.values()) + ' |')
    lines += ['', 'GUI 返回时间不等于屏幕实际呈现时间；source 时间不含曝光和摄像头内部缓存。',
              '编码输出时间不含无线传输、安卓解码和显示。不得将不同组的中位数直接相减解释为精确阶段耗时。']
    (arguments.run_directory / 'SUMMARY.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print('\n'.join(lines), flush=True)


if __name__ == '__main__':
    main()
