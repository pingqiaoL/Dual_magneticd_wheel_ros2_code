#!/usr/bin/env python3
"""Run controlled tests in the container and restore the known original entrypoints."""

import argparse
import csv
import json
import os
import signal
import subprocess
import time
from pathlib import Path

ROOT = Path('/workspace')
SUITE = Path(__file__).resolve().parent


def processes():
    result = {}
    for path in Path('/proc').iterdir():
        if not path.name.isdigit():
            continue
        try:
            args = [os.fsdecode(value) for value in (path / 'cmdline').read_bytes().split(b'\0') if value]
            result[int(path.name)] = args
        except (FileNotFoundError, PermissionError, ProcessLookupError):
            pass
    return result


def stop_tree(process_id):
    subprocess.run(['bash', str(ROOT / 'activate/internal/stop_launch_tree.sh'), str(process_id)],
                   check=True, timeout=25)


def start_logged(command, path, environment):
    with path.open('w') as stream:
        return subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT,
                                start_new_session=True, env=environment)


def resource_row():
    fields = Path('/proc/stat').read_text().splitlines()[0].split()[1:9]
    ticks = [int(value) for value in fields]
    memory = {line.split(':')[0]: line.split(':')[1].strip()
              for line in Path('/proc/meminfo').read_text().splitlines()}
    thermal = {}
    for directory in Path('/sys/class/thermal').glob('thermal_zone*'):
        try:
            thermal[(directory / 'type').read_text().strip()] = int((directory / 'temp').read_text())
        except (OSError, ValueError):
            pass
    frequencies = {}
    for path in Path('/sys/devices/system/cpu').glob('cpu[0-9]*/cpufreq/scaling_cur_freq'):
        try:
            frequencies[path.parent.parent.name] = int(path.read_text())
        except OSError:
            pass
    return [time.monotonic_ns(), sum(ticks), ticks[3] + ticks[4],
            memory.get('MemAvailable', ''), json.dumps(thermal), json.dumps(frequencies)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--runtime-root', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=60)
    parser.add_argument('--warmup', type=float, default=10)
    parser.add_argument('--run-directory', type=Path)
    parser.add_argument('--groups', nargs='+', default=['A_headless', 'B_legacy_cv', 'C_latest_cv', 'D_video', 'E_full'])
    arguments = parser.parse_args()
    run_root = arguments.run_directory or SUITE / 'results' / time.strftime('%Y%m%d_%H%M%S')
    run_root.mkdir(parents=True, exist_ok=bool(arguments.run_directory))
    if arguments.run_directory:
        # 恢复旧版本 runner 已完整采集、但尚无组完成标记的结果。
        for directory in run_root.iterdir():
            if directory.name not in ['A_headless', 'B_legacy_cv', 'C_latest_cv', 'D_video', 'E_full']:
                continue
            if directory.name in arguments.groups or (directory / 'completed').exists():
                continue
            metadata = json.loads((directory / 'metadata.json').read_text())
            with (directory / 'resources.csv').open() as stream:
                resources = list(csv.DictReader(stream))
            if (not (directory / 'viewer.csv').exists() or not resources or
                    int(resources[-1]['monotonic_ns']) < metadata['measurement_end_ns'] - 2000000000):
                raise RuntimeError(f'Cannot reuse incomplete group: {directory}')
            (directory / 'completed').write_text('full interval and viewer output verified on resume\n')
    original_processes = processes()
    managers = [pid for pid, args in original_processes.items()
                if any(value.endswith('/activate/mix_module/camera_thickness.sh') or
                       value == 'activate/mix_module/camera_thickness.sh' for value in args)]
    viewers = [pid for pid, args in original_processes.items()
               if any(value.endswith('/tools/view_raw_image.py') for value in args)]
    if len(managers) > 1 or len(viewers) > 1:
        raise RuntimeError('Unexpected multiple sessions; preserve current state and inspect first')
    # 原入口参数保留；环境使用当前容器的桌面 DISPLAY。
    restore_commands = []
    display_environment = {}
    for pid in managers + viewers:
        args = original_processes[pid]
        values = {}
        for item in Path(f'/proc/{pid}/environ').read_bytes().split(b'\0'):
            if b'=' in item:
                key, value = item.split(b'=', 1)
                if key in (b'DISPLAY', b'XAUTHORITY', b'QT_X11_NO_MITSHM'):
                    values[os.fsdecode(key)] = os.fsdecode(value)
        restore_commands.append(dict(command=args, environment=values))
        if pid in viewers:
            display_environment = values
    restoration = {'commands': restore_commands, 'display': os.environ.get('DISPLAY'),
                   'ros_domain_id': os.environ.get('ROS_DOMAIN_ID')}
    (run_root / 'original_state.json').write_text(json.dumps(restoration, indent=2))
    print('RESULTS=' + str(run_root), flush=True)
    children = []
    try:
        for pid in viewers + managers:
            stop_tree(pid)
        time.sleep(2)
        remaining = [args for args in processes().values()
                     if args and Path(args[0]).name in ('component_container_mt', 'thickness_node',
                                                        'measurement_manager_node', 'telemetry_bridge_node')]
        if remaining:
            raise RuntimeError(f'Unexpected nodes still active: {remaining}')

        overlay = arguments.runtime_root / 'install/setup.bash'
        for group, mode, video, thickness, depth in [
            ('A_headless', 'headless', False, False, 1),
            ('B_legacy_cv', 'legacy', False, False, 5),
            ('C_latest_cv', 'latest', False, False, 1),
            ('D_video', 'headless', True, False, 1),
            ('E_full', 'latest', True, True, 1),
        ]:
            if group not in arguments.groups:
                continue
            directory = run_root / group
            if directory.exists():
                directory.rename(run_root / ('failed_' + group + '_' + time.strftime('%H%M%S')))
            directory.mkdir()
            environment = os.environ.copy()
            environment.update(display_environment)
            environment['CAMERA_DELAY_OUTPUT'] = str(directory)
            environment['ROS_DOMAIN_ID'] = '79'
            environment['QT_X11_NO_MITSHM'] = '1'
            started_ns = time.monotonic_ns()
            metadata = dict(group=group, mode=mode, depth=depth, video=video, thickness=thickness,
                            started_ns=started_ns, measurement_start_ns=started_ns + int(arguments.warmup * 1e9),
                            measurement_end_ns=started_ns + int((arguments.warmup + arguments.seconds) * 1e9),
                            seconds=arguments.seconds, warmup=arguments.warmup)
            (directory / 'metadata.json').write_text(json.dumps(metadata, indent=2))
            video_component = '''<composable_node pkg="video_streamer_transmission" plugin="video_streamer_transmission::VideoStreamerNode" name="video_streamer_node">
              <param from="/workspace/src/video_streamer_transmission/config/video.yaml" />
              <param name="bitrate_kbps" value="6000" />
              <extra_arg name="use_intra_process_comms" value="true" />
            </composable_node>''' if video else ''
            launch_file = directory / 'test.launch.xml'
            launch_file.write_text('''<launch>
              <node_container pkg="rclcpp_components" exec="component_container_mt" name="delay_video_container" namespace="/" output="screen">
                <composable_node pkg="camera_driver" plugin="camera_driver::CameraCaptureNode" name="camera_capture_node">
                  <param from="/workspace/src/video_streamer_transmission/config/video.yaml" />
                  <extra_arg name="use_intra_process_comms" value="true" />
                </composable_node>''' + video_component + '''</node_container></launch>''')
            # 路径为本脚本生成的固定绝对路径，不接受外部 shell 文本。
            command = f'source {overlay} && exec ros2 launch {launch_file}'
            launch = start_logged(['bash', '-c', command], directory / 'launch.log', environment)
            children.append(launch)
            ready_message = 'RTSP :8554/live' if video else "Loaded node '/camera_capture_node'"
            startup_deadline = time.monotonic() + 20
            while ready_message not in (directory / 'launch.log').read_text():
                if launch.poll() is not None or time.monotonic() > startup_deadline:
                    raise RuntimeError((directory / 'launch.log').read_text())
                time.sleep(0.1)
            started_ns = time.monotonic_ns()
            metadata.update(started_ns=started_ns,
                            measurement_start_ns=started_ns + int(arguments.warmup * 1e9),
                            measurement_end_ns=started_ns + int((arguments.warmup + arguments.seconds) * 1e9))
            (directory / 'metadata.json').write_text(json.dumps(metadata, indent=2))
            duration = arguments.warmup + arguments.seconds + 1
            viewer = start_logged(['/usr/local/bin/robot-entrypoint', '/opt/robot-venv/bin/python3',
                                   str(SUITE / 'measure_viewer.py'), '--mode', mode, '--depth', str(depth),
                                   '--duration', str(duration), '--output', str(directory / 'viewer.csv')],
                                  directory / 'viewer.log', environment)
            children.append(viewer)
            client = None
            if video:
                client = start_logged(['gst-launch-1.0', '-q', 'rtspsrc',
                                       'location=rtsp://127.0.0.1:8554/live', 'protocols=tcp', 'latency=0',
                                       '!', 'rtph264depay', '!', 'fakesink', 'sync=false'],
                                      directory / 'rtsp_client.log', environment)
                children.append(client)
            thickness_process = None
            if thickness:
                thickness_process = start_logged(['bash', str(ROOT / 'activate/single_module/thickness.sh')],
                                                  directory / 'thickness.log', environment)
                children.append(thickness_process)
            print('START ' + group, flush=True)
            with (directory / 'resources.csv').open('w', newline='') as stream:
                writer = csv.writer(stream)
                writer.writerow(['monotonic_ns', 'cpu_total_ticks', 'cpu_idle_ticks', 'mem_available', 'thermal_millic', 'frequency_khz'])
                deadline = started_ns / 1e9 + arguments.warmup + arguments.seconds
                next_report = time.monotonic() + 20
                while time.monotonic() < deadline:
                    if launch.poll() is not None or viewer.poll() is not None:
                        raise RuntimeError(f'Unexpected early exit in {group}; inspect logs')
                    if client and client.poll() is not None:
                        raise RuntimeError((directory / 'rtsp_client.log').read_text())
                    writer.writerow(resource_row())
                    if time.monotonic() >= next_report:
                        print('RUNNING ' + group, flush=True)
                        next_report += 20
                    time.sleep(1)
            viewer.send_signal(signal.SIGINT)
            viewer.wait(timeout=10)
            for child in [client, thickness_process, launch]:
                if child and child.poll() is None:
                    stop_tree(child.pid)
                    child.wait(timeout=20)
            children.clear()
            (directory / 'completed').write_text('group complete\n')
            print('DONE ' + group, flush=True)
        if all((run_root / group / 'completed').exists() for group in
               ['A_headless', 'B_legacy_cv', 'C_latest_cv', 'D_video', 'E_full']):
            (run_root / 'completed').write_text('matrix complete\n')
    finally:
        for child in reversed(children):
            if child.poll() is None:
                stop_tree(child.pid)
                child.wait(timeout=20)
        for index, original in enumerate(restore_commands):
            command = original['command']
            environment = os.environ.copy()
            environment.update(original['environment'])
            restored = start_logged(['/usr/local/bin/robot-entrypoint'] + command,
                                    run_root / f'restored_{index}.log', environment)
            print(f'RESTORED pid={restored.pid} command={command}', flush=True)
        (run_root / 'restored').write_text('original entrypoints restarted; verify logs\n')


if __name__ == '__main__':
    main()
