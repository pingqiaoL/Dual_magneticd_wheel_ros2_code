#!/usr/bin/env python3
"""Summarize 1080p bitrate variants from exact frame traces."""

import argparse
import csv
import json
import math
import statistics
import sys
from pathlib import Path

SUITE = Path(__file__).resolve().parent
sys.path.insert(0, str(SUITE))
from analyze import analyze  # noqa: E402


def distribution(values):
    ordered = sorted(values)
    if not ordered:
        return {"count": 0}
    return {
        "count": len(ordered),
        "median": statistics.median(ordered),
        "p95": ordered[max(0, math.ceil(len(ordered) * 0.95) - 1)],
        "maximum": ordered[-1],
    }


def resource_metrics(directory, start_ns, end_ns):
    with (directory / "resources.csv").open() as stream:
        rows = list(csv.DictReader(stream))
    tx_mbps = []
    drops = {name: 0 for name in ("eth0_tx_dropped", "eth0_rx_dropped", "eth0_tx_errors", "eth0_rx_errors")}
    for previous, current in zip(rows, rows[1:]):
        current_ns = int(current["monotonic_ns"])
        if not start_ns <= current_ns < end_ns:
            continue
        elapsed = (current_ns - int(previous["monotonic_ns"])) / 1e9
        byte_delta = int(current["eth0_tx_bytes"]) - int(previous["eth0_tx_bytes"])
        if elapsed > 0 and byte_delta >= 0:
            tx_mbps.append(byte_delta * 8 / elapsed / 1e6)
    if rows:
        for name in drops:
            drops[name] = int(rows[-1][name]) - int(rows[0][name])
    return {"eth0_tx_mbps": distribution(tx_mbps), "network_counter_delta": drops}


def format_pair(metric):
    if not metric or not metric.get("count"):
        return "—"
    return f"{metric['median']:.2f} / {metric['p95']:.2f}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("run_directory", type=Path)
    arguments = parser.parse_args()
    if not (arguments.run_directory / "completed").exists():
        raise RuntimeError("Test did not complete; refusing to summarize partial data")

    results = {}
    for directory in sorted(arguments.run_directory.glob("1080p30_*")):
        if not (directory / "completed").exists():
            raise RuntimeError(f"Incomplete variant: {directory}")
        result = analyze(directory)
        metadata = result["metadata"]
        result["resources"] = resource_metrics(
            directory, metadata["measurement_start_ns"], metadata["measurement_end_ns"]
        )
        results[directory.name] = result
    (arguments.run_directory / "summary.json").write_text(json.dumps(results, indent=2))

    lines = [
        "# 1080p30 编码参数延迟测试",
        "",
        "单位为 ms；延迟列为中位数 / P95。软件时间从 V4L2 输出开始，不含曝光、摄像头内部缓存、无线链路和遥控器显示。",
        "",
        "| 配置 | 实际采集 FPS | 实际编码 FPS | V4L2→H.264 | 编码与解析 | CPU | eth0 发送 Mbit/s |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    for name, result in results.items():
        lines.append(
            "| "
            + " | ".join(
                [
                    name,
                    f"{result['fps'].get('source', 0):.2f}",
                    f"{result['fps'].get('encoded', 0):.2f}",
                    format_pair(result["metrics"].get("source_to_encoded_ms")),
                    format_pair(result["metrics"].get("encode_and_parse_ms")),
                    format_pair(result.get("cpu_total_percent")),
                    format_pair(result["resources"].get("eth0_tx_mbps")),
                ]
            )
            + " |"
        )
    lines.extend(
        [
            "",
            "CPU 为整机全部核心总能力百分比。eth0 数据只有在遥控器实际连接测试流时才代表图传流量；本机回环接收不经过 eth0。",
            "",
            "完整画面延迟仍需用同屏计时器或高帧率录像测量；本报告用于确认 1080p 是否能保持 30 FPS，以及码率变化是否增加 Nano 内部等待。",
        ]
    )
    report = "\n".join(lines) + "\n"
    (arguments.run_directory / "REPORT.md").write_text(report, encoding="utf-8")
    print(report)


if __name__ == "__main__":
    main()
