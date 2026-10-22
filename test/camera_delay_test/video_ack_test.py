#!/usr/bin/env python3
"""Nano test-only RTP/serial closed-loop collector; Android runs VideoArrivalProbe."""

import argparse
import csv
import json
import os
import struct
import time
from datetime import datetime, timezone, timedelta
from pathlib import Path

import serial
from pymavlink.dialects.v20 import common as mavlink
from pymavlink.generator.mavcrc import x25crc
from run_matrix import processes, start_logged, stop_tree


def full_ping(endpoint, token, sequence):
    packet = bytearray([253, 14, 0, 0, endpoint.seq, 1, 191, 4, 0, 0])
    packet.extend(struct.pack('<QIBB', token, sequence, 0, 0))
    checksum = x25crc(bytes(packet[1:]))
    checksum.accumulate(bytes([237]))
    packet.extend(struct.pack('<H', checksum.crc))
    endpoint.file.write(packet)
    endpoint.seq = (endpoint.seq + 1) % 256


def collect(output, seconds, warmup):
    records = []
    pending = {}
    first_ack = None
    next_ping = time.monotonic()
    next_heartbeat = next_ping
    sequence = 0
    wait_deadline = time.monotonic() + 40
    with serial.Serial('/dev/ttyTHS0', 115200, timeout=0.005, write_timeout=0.2, exclusive=True) as port:
        endpoint = mavlink.MAVLink(port, srcSystem=1, srcComponent=191)
        endpoint.robust_parsing = True
        try:
            while True:
                now = time.monotonic()
                if now >= next_heartbeat:
                    endpoint.heartbeat_send(mavlink.MAV_TYPE_ONBOARD_CONTROLLER,
                                            mavlink.MAV_AUTOPILOT_INVALID, 0, 0,
                                            mavlink.MAV_STATE_STANDBY)
                    next_heartbeat = now + 1
                if first_ack is None and now >= wait_deadline:
                    raise RuntimeError('No video ACK within 40 s; verify Android probe and Ethernet')
                if first_ack is not None and time.monotonic_ns() >= first_ack + int((warmup + seconds + 1) * 1e9):
                    break
                if now >= next_ping:
                    sent = time.monotonic_ns()
                    token = sent // 1000
                    pending[(sequence, token)] = sent
                    full_ping(endpoint, token, sequence)
                    sequence += 1
                    next_ping = now + 1
                data = port.read(max(1, port.in_waiting))
                for message in endpoint.parse_buffer(data) or []:
                    received = time.monotonic_ns()
                    if (message.get_type() != 'PING' or message.get_srcSystem() != 255 or
                            message.get_srcComponent() != 190):
                        continue
                    if message.target_system != 1 or message.target_component != 191:
                        continue
                    if message.seq & 0x80000000:
                        if first_ack is None:
                            first_ack = received
                            print('FIRST_VIDEO_ACK; starting warmup', flush=True)
                        records.append([received, 'video_ack', message.time_usec, message.seq & 0x7fffffff, 0])
                        print('VIDEO_ACK key=' + str(message.time_usec), flush=True)
                    else:
                        sent = pending.pop((message.seq, message.time_usec), None)
                        if sent is not None:
                            records.append([received, 'ping_reply', message.time_usec, message.seq, sent])
                for key, sent in list(pending.items()):
                    if time.monotonic_ns() - sent > 2000000000:
                        records.append([time.monotonic_ns(), 'ping_timeout', key[1], key[0], sent])
                        del pending[key]
        finally:
            with (output / 'uart.csv').open('w', newline='') as stream:
                writer = csv.writer(stream)
                writer.writerow(['received_ns', 'event', 'token_or_frame_key', 'processing_us_or_seq', 'sent_ns'])
                writer.writerows(records)
            metadata = dict(seconds=seconds, warmup=warmup, first_ack_ns=first_ack,
                            measurement_start_ns=first_ack + int(warmup * 1e9) if first_ack else None,
                            measurement_end_ns=first_ack + int((warmup + seconds) * 1e9) if first_ack else None,
                            video='1280x720 MJPEG capture, x264 veryfast zerolatency 6000 kbit/s GOP30',
                            endpoint='test Java UDP receiver on eth0; no decoder or renderer',
                            uart='/dev/ttyTHS0 115200 8N1', ros_domain_id=79)
            (output / 'metadata.json').write_text(json.dumps(metadata, indent=2))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--runtime-root', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=60)
    parser.add_argument('--warmup', type=float, default=10)
    parser.add_argument('--output', type=Path)
    arguments = parser.parse_args()
    suite = Path(__file__).resolve().parent
    timestamp = datetime.now(timezone(timedelta(hours=8))).strftime('%Y%m%d_%H%M%S')
    output = arguments.output or suite / 'results' / ('video_ack_' + timestamp)
    output.mkdir(parents=True)
    current = processes()
    originals = [(pid, command) for pid, command in current.items()
                 if '/workspace/activate/mix_module/camera_thickness.sh' in command]
    if len(originals) != 1:
        raise RuntimeError('Expected one original combined entrypoint; no automatic process changes')
    launch = None
    print('RESULTS=' + str(output), flush=True)
    try:
        stop_tree(originals[0][0])
        environment = os.environ.copy()
        environment['CAMERA_DELAY_OUTPUT'] = str(output)
        environment['ROS_DOMAIN_ID'] = '79'
        overlay = arguments.runtime_root / 'install/setup.bash'
        # Fixed generated paths; no externally supplied command fragments.
        command = f'source {overlay} && exec ros2 launch video_streamer_transmission video.launch.xml'
        launch = start_logged(['bash', '-c', command], output / 'video.log', environment)
        deadline = time.monotonic() + 20
        while 'RTSP :8554/live' not in (output / 'video.log').read_text():
            if launch.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError((output / 'video.log').read_text())
            time.sleep(0.1)
        print('READY: start Android test receiver now', flush=True)
        collect(output, arguments.seconds, arguments.warmup)
        (output / 'completed').write_text('UART collection complete; analyze exact RTP matches\n')
        # Let the 75-second Android probe send TEARDOWN before the test server exits.
        time.sleep(8)
    finally:
        if launch and launch.poll() is None:
            stop_tree(launch.pid)
            launch.wait(timeout=20)
        restored = start_logged(['/usr/local/bin/robot-entrypoint'] + originals[0][1],
                                output / 'restored.log', os.environ.copy())
        print('RESTORED pid=' + str(restored.pid), flush=True)


if __name__ == '__main__':
    main()
