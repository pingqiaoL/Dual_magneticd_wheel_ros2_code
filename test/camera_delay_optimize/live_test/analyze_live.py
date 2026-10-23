#!/usr/bin/env python3
import csv
import importlib.util
import json
import sys
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "delay_analyze", "/workspace/test/camera_delay_optimize/analyze.py"
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

for argument in sys.argv[1:]:
    directory = Path(argument)
    timestamps = []
    for path in directory.glob("trace_*.csv"):
        with path.open() as stream:
            for row in csv.reader(stream):
                if len(row) != 4:
                    continue
                try:
                    timestamps.append(int(row[0]))
                except ValueError:
                    pass
    if not timestamps:
        continue
    end = max(timestamps)
    seconds = min(20.0, (end - min(timestamps)) / 1e9)
    metadata = {
        "group": directory.name,
        "width": 1920,
        "height": 1080,
        "frame_rate": 30,
        "bitrate_kbps": 6000,
        "encoder_variant": directory.name,
        "encoder_options": "",
        "vision_branch": True,
        "warmup": 0,
        "seconds": seconds,
        "started_ns": end - int(seconds * 1e9),
        "measurement_start_ns": end - int(seconds * 1e9),
        "measurement_end_ns": end + 1,
    }
    (directory / "metadata.json").write_text(json.dumps(metadata))
    (directory / "resources.csv").write_text(
        "monotonic_ns,cpu_total_ticks,cpu_idle_ticks,mem_available,eth0_tx_bytes,"
        "eth0_rx_bytes,eth0_tx_dropped,eth0_rx_dropped,eth0_tx_errors,"
        "eth0_rx_errors,thermal_millic\n"
    )
    result = module.analyze(directory)
    wanted = {
        name: values
        for name, values in result["metrics"].items()
        if name
        in (
            "capture_pipeline_ms",
            "video_wait_and_copy_ms",
            "encode_and_parse_ms",
            "source_to_encoded_ms",
        )
    }
    print(
        json.dumps(
            {
                "name": directory.name,
                "metrics": wanted,
                "fps": {
                    name: result["fps"].get(name)
                    for name in ("source", "publish", "push", "encoded")
                },
            },
            indent=2,
        )
    )
