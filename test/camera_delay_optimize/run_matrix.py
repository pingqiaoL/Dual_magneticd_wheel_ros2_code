#!/usr/bin/env python3
"""Measure isolated 1080p video variants and restore the original robot entrypoint."""

import argparse
import csv
import json
import os
import signal
import subprocess
import time
from pathlib import Path

ROOT = Path("/workspace")


def processes():
    result = {}
    for path in Path("/proc").iterdir():
        if not path.name.isdigit():
            continue
        try:
            args = [os.fsdecode(value) for value in (path / "cmdline").read_bytes().split(b"\0") if value]
            result[int(path.name)] = args
        except (FileNotFoundError, PermissionError, ProcessLookupError):
            pass
    return result


def stop_tree(process_id):
    subprocess.run(
        ["bash", str(ROOT / "activate/internal/stop_launch_tree.sh"), str(process_id)],
        check=True,
        timeout=25,
    )


def start_logged(command, path, environment):
    stream = path.open("w")
    process = subprocess.Popen(
        command,
        stdout=stream,
        stderr=subprocess.STDOUT,
        start_new_session=True,
        env=environment,
    )
    stream.close()
    return process


def read_integer(path):
    try:
        return int(path.read_text())
    except (OSError, ValueError):
        return -1


def resource_row():
    fields = Path("/proc/stat").read_text().splitlines()[0].split()[1:9]
    ticks = [int(value) for value in fields]
    memory = {
        line.split(":")[0]: line.split(":")[1].strip()
        for line in Path("/proc/meminfo").read_text().splitlines()
    }
    network = Path("/sys/class/net/eth0/statistics")
    thermal = {}
    for directory in Path("/sys/class/thermal").glob("thermal_zone*"):
        try:
            thermal[(directory / "type").read_text().strip()] = int(
                (directory / "temp").read_text()
            )
        except (OSError, ValueError):
            pass
    return [
        time.monotonic_ns(),
        sum(ticks),
        ticks[3] + ticks[4],
        memory.get("MemAvailable", ""),
        read_integer(network / "tx_bytes"),
        read_integer(network / "rx_bytes"),
        read_integer(network / "tx_dropped"),
        read_integer(network / "rx_dropped"),
        read_integer(network / "tx_errors"),
        read_integer(network / "rx_errors"),
        json.dumps(thermal),
    ]


def find_original_entrypoints(all_processes):
    managers = []
    for pid, args in all_processes.items():
        if any(
            value.endswith("/activate/mix_module/camera_thickness.sh")
            or value.endswith("/activate/robot.sh")
            for value in args
        ):
            managers.append(pid)
    if len(managers) > 1:
        raise RuntimeError(f"More than one robot entrypoint is active: {managers}")
    return managers


def wait_for_log(process, log_path, expected, timeout_seconds=25):
    deadline = time.monotonic() + timeout_seconds
    while expected not in log_path.read_text(errors="replace"):
        if process.poll() is not None:
            raise RuntimeError(log_path.read_text(errors="replace"))
        if time.monotonic() >= deadline:
            raise RuntimeError(
                f"Timed out waiting for {expected}:\n{log_path.read_text(errors='replace')}"
            )
        time.sleep(0.1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--runtime-root", type=Path, required=True)
    parser.add_argument("--run-directory", type=Path, required=True)
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    parser.add_argument("--frame-rate", type=int, default=30)
    parser.add_argument("--bitrates-kbps", type=int, nargs="+", default=[6000, 8000])
    parser.add_argument("--warmup", type=float, default=5)
    parser.add_argument("--seconds", type=float, default=30)
    arguments = parser.parse_args()

    run_root = arguments.run_directory
    run_root.mkdir(parents=True)
    original_processes = processes()
    managers = find_original_entrypoints(original_processes)
    restore_commands = []
    for pid in managers:
        values = {}
        for item in Path(f"/proc/{pid}/environ").read_bytes().split(b"\0"):
            if b"=" not in item:
                continue
            key, value = item.split(b"=", 1)
            if key in (b"DISPLAY", b"XAUTHORITY", b"QT_X11_NO_MITSHM"):
                values[os.fsdecode(key)] = os.fsdecode(value)
        restore_commands.append({"command": original_processes[pid], "environment": values})
    (run_root / "original_state.json").write_text(
        json.dumps({"commands": restore_commands}, indent=2)
    )
    print(f"RESULTS={run_root}", flush=True)

    children = []
    try:
        for pid in managers:
            stop_tree(pid)
        time.sleep(2)
        remaining = [
            args
            for args in processes().values()
            if args
            and Path(args[0]).name
            in (
                "component_container_mt",
                "thickness_node",
                "measurement_manager_node",
                "telemetry_bridge_node",
            )
        ]
        if remaining:
            raise RuntimeError(f"Unexpected nodes still active: {remaining}")

        overlay = arguments.runtime_root / "install/setup.bash"
        for bitrate_kbps in arguments.bitrates_kbps:
            group = f"1080p_{bitrate_kbps}kbps"
            directory = run_root / group
            directory.mkdir()
            environment = os.environ.copy()
            environment["CAMERA_DELAY_OUTPUT"] = str(directory)
            environment["ROS_DOMAIN_ID"] = "79"
            started_ns = time.monotonic_ns()
            metadata = {
                "group": group,
                "width": arguments.width,
                "height": arguments.height,
                "frame_rate": arguments.frame_rate,
                "bitrate_kbps": bitrate_kbps,
                "warmup": arguments.warmup,
                "seconds": arguments.seconds,
                "started_ns": started_ns,
                "measurement_start_ns": started_ns + int(arguments.warmup * 1e9),
                "measurement_end_ns": started_ns + int((arguments.warmup + arguments.seconds) * 1e9),
            }
            (directory / "metadata.json").write_text(json.dumps(metadata, indent=2))
            launch_file = directory / "test.launch.xml"
            launch_file.write_text(
                f'''<launch>
  <node_container pkg="rclcpp_components" exec="component_container_mt" name="delay_video_container" namespace="/" output="screen">
    <composable_node pkg="camera_driver" plugin="camera_driver::CameraCaptureNode" name="camera_capture_node">
      <param from="/workspace/src/video_streamer_transmission/config/video.yaml" />
      <param name="source_mode" value="camera" />
      <param name="device_path" value="/dev/video0" />
      <param name="input_format" value="MJPEG" />
      <param name="width" value="{arguments.width}" />
      <param name="height" value="{arguments.height}" />
      <param name="frame_rate" value="{arguments.frame_rate}" />
      <extra_arg name="use_intra_process_comms" value="true" />
    </composable_node>
    <composable_node pkg="video_streamer_transmission" plugin="video_streamer_transmission::VideoStreamerNode" name="video_streamer_node">
      <param from="/workspace/src/video_streamer_transmission/config/video.yaml" />
      <param name="width" value="{arguments.width}" />
      <param name="height" value="{arguments.height}" />
      <param name="frame_rate" value="{arguments.frame_rate}" />
      <param name="bitrate_kbps" value="{bitrate_kbps}" />
      <param name="rtsp_port" value="8554" />
      <extra_arg name="use_intra_process_comms" value="true" />
    </composable_node>
  </node_container>
</launch>
'''
            )
            command = f"source {overlay} && exec ros2 launch {launch_file}"
            launch = start_logged(
                ["bash", "-c", command], directory / "launch.log", environment
            )
            children.append(launch)
            wait_for_log(launch, directory / "launch.log", "RTSP :8554/live")

            client = start_logged(
                [
                    "gst-launch-1.0",
                    "-q",
                    "rtspsrc",
                    "location=rtsp://127.0.0.1:8554/live",
                    "protocols=udp",
                    "latency=0",
                    "!",
                    "rtph264depay",
                    "!",
                    "fakesink",
                    "sync=false",
                ],
                directory / "rtsp_client.log",
                environment,
            )
            children.append(client)
            started_ns = time.monotonic_ns()
            metadata.update(
                started_ns=started_ns,
                measurement_start_ns=started_ns + int(arguments.warmup * 1e9),
                measurement_end_ns=started_ns + int((arguments.warmup + arguments.seconds) * 1e9),
            )
            (directory / "metadata.json").write_text(json.dumps(metadata, indent=2))

            print(f"START {group}", flush=True)
            with (directory / "resources.csv").open("w", newline="") as stream:
                writer = csv.writer(stream)
                writer.writerow(
                    [
                        "monotonic_ns",
                        "cpu_total_ticks",
                        "cpu_idle_ticks",
                        "mem_available",
                        "eth0_tx_bytes",
                        "eth0_rx_bytes",
                        "eth0_tx_dropped",
                        "eth0_rx_dropped",
                        "eth0_tx_errors",
                        "eth0_rx_errors",
                        "thermal_millic",
                    ]
                )
                deadline = time.monotonic() + arguments.warmup + arguments.seconds
                while time.monotonic() < deadline:
                    if launch.poll() is not None:
                        raise RuntimeError((directory / "launch.log").read_text())
                    if client.poll() is not None:
                        raise RuntimeError((directory / "rtsp_client.log").read_text())
                    writer.writerow(resource_row())
                    stream.flush()
                    time.sleep(1)

            for child in [client, launch]:
                if child.poll() is None:
                    stop_tree(child.pid)
                    child.wait(timeout=20)
                children.remove(child)
            (directory / "completed").write_text("group complete\n")
            print(f"DONE {group}", flush=True)
        (run_root / "completed").write_text("all variants complete\n")
    finally:
        for child in reversed(children):
            if child.poll() is None:
                try:
                    stop_tree(child.pid)
                    child.wait(timeout=20)
                except (subprocess.SubprocessError, OSError):
                    child.send_signal(signal.SIGTERM)
        for index, original in enumerate(restore_commands):
            environment = os.environ.copy()
            environment.update(original["environment"])
            restored = start_logged(
                ["/usr/local/bin/robot-entrypoint"] + original["command"],
                run_root / f"restored_{index}.log",
                environment,
            )
            print(f"RESTORED pid={restored.pid} command={original['command']}", flush=True)
        (run_root / "restored").write_text("original entrypoint restarted\n")


if __name__ == "__main__":
    main()
