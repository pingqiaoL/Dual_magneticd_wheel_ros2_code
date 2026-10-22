#!/usr/bin/env python3
"""Measure MAVLink PING RTT on the existing UART, then restore the original entrypoint."""

import argparse
import csv
import json
import os
import select
import statistics
import struct
import termios
import time
from pathlib import Path

from pymavlink.dialects.v20 import common as mavlink
from pymavlink.generator.mavcrc import x25crc
from run_matrix import processes, start_logged, stop_tree


def measure(device, count, output):
    descriptor = os.open(device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    previous = termios.tcgetattr(descriptor)
    config = termios.tcgetattr(descriptor)
    config[0] = config[1] = config[3] = 0
    config[2] = termios.CS8 | termios.CLOCAL | termios.CREAD
    config[4] = config[5] = termios.B115200
    config[6][termios.VMIN] = config[6][termios.VTIME] = 0
    termios.tcsetattr(descriptor, termios.TCSANOW, config)
    rows, pending, inbound, peers, observed_pings = [], {}, {}, {}, []

    class Writer:
        def write(self, frame):
            offset = 0
            deadline = time.monotonic() + 0.2
            while offset < len(frame):
                if time.monotonic() > deadline:
                    raise TimeoutError('UART write timeout')
                if select.select([], [descriptor], [], 0.005)[1]:
                    offset += os.write(descriptor, frame[offset:])
            return offset

    endpoint = mavlink.MAVLink(Writer(), srcSystem=1, srcComponent=191)
    endpoint.robust_parsing = True
    started = time.monotonic()
    next_send, next_heartbeat = started + 3, started
    sent_count = 0
    last_reported = 0
    try:
        while sent_count < count or pending:
            now = time.monotonic()
            if now >= next_heartbeat:
                endpoint.heartbeat_send(mavlink.MAV_TYPE_ONBOARD_CONTROLLER,
                                        mavlink.MAV_AUTOPILOT_INVALID, 0, 0,
                                        mavlink.MAV_STATE_STANDBY)
                next_heartbeat = now + 1
            if sent_count < count and now >= next_send:
                timestamp_ns = time.monotonic_ns()
                token = timestamp_ns // 1000
                pending[(sent_count, token)] = timestamp_ns
                # 安卓旧版只接受 14 B PING 负载；完整零尾部同样是有效 MAVLink 2。
                payload = struct.pack('<QIBB', token, sent_count, 0, 0)
                packet = bytearray([0xFD, 14, 0, 0, endpoint.seq, 1, 191, 4, 0, 0])
                packet.extend(payload)
                checksum = x25crc(bytes(packet[1:]))
                checksum.accumulate(bytes([237]))
                packet.extend(struct.pack('<H', checksum.crc))
                Writer().write(packet)
                endpoint.seq = (endpoint.seq + 1) % 256
                sent_count += 1
                next_send = now + 1
            if select.select([descriptor], [], [], 0.005)[0]:
                frame_bytes = os.read(descriptor, 4096)
                for byte in frame_bytes:
                    message = endpoint.parse_char(bytes([byte]))
                    if message is None:
                        continue
                    received_ns = time.monotonic_ns()
                    name = message.get_type()
                    inbound[name] = inbound.get(name, 0) + 1
                    if name != 'BAD_DATA':
                        peer = f'{message.get_srcSystem()}/{message.get_srcComponent()}:{name}'
                        peers[peer] = peers.get(peer, 0) + 1
                    if name != 'PING':
                        continue
                    if len(observed_pings) < 30:
                        observed_pings.append(dict(system=message.get_srcSystem(), component=message.get_srcComponent(),
                                                   sequence=message.seq, token=message.time_usec,
                                                   target_system=message.target_system, target_component=message.target_component))
                    if message.target_system == 0 and message.target_component == 0:
                        endpoint.ping_send(message.time_usec, message.seq,
                                           message.get_srcSystem(), message.get_srcComponent())
                    elif (message.target_system == 1 and message.target_component == 191 and
                          message.get_srcSystem() == 255 and message.get_srcComponent() == 190):
                        key = (message.seq, message.time_usec)
                        sent_ns = pending.pop(key, None)
                        if sent_ns is not None:
                            rows.append([message.seq, message.time_usec, sent_ns, received_ns,
                                         (received_ns - sent_ns) / 1e6, 'reply'])
            for key, sent_ns in list(pending.items()):
                if time.monotonic_ns() - sent_ns > 2000000000:
                    rows.append([key[0], key[1], sent_ns, '', '', 'timeout'])
                    del pending[key]
            if sent_count and sent_count % 10 == 0 and sent_count != last_reported:
                print(f'sent={sent_count} matched={sum(row[-1] == "reply" for row in rows)}', flush=True)
                last_reported = sent_count
    finally:
        termios.tcsetattr(descriptor, termios.TCSANOW, previous)
        os.close(descriptor)
        with (output / 'samples.csv').open('w', newline='') as stream:
            writer = csv.writer(stream)
            writer.writerow(['sequence', 'token_us', 'sent_monotonic_ns', 'received_monotonic_ns', 'rtt_ms', 'status'])
            writer.writerows(sorted(rows))
    values = sorted(row[4] for row in rows if row[-1] == 'reply')
    summary = dict(device=device, baud_rate=115200, requested=count, matched=len(values),
                   timeout=count - len(values), inbound_messages=inbound,
                   peers=peers, observed_pings=observed_pings, request_payload_bytes=14,
                   conditions='video and thickness paused; no movement commands sent',
                   definition='application RTT including both UART/wireless directions and remote processing')
    if values:
        summary.update(minimum_ms=min(values), median_ms=statistics.median(values),
                       mean_ms=statistics.mean(values), p95_ms=values[max(0, int(len(values) * 0.95 + 0.999) - 1)],
                       maximum_ms=max(values))
    (output / 'summary.json').write_text(json.dumps(summary, indent=2))
    print(json.dumps({key: value for key, value in summary.items() if key != 'observed_pings'}, indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--device', default='/dev/ttyTHS0')
    parser.add_argument('--count', type=int, default=60)
    arguments = parser.parse_args()
    if arguments.count < 1:
        parser.error('--count must be positive')
    output = Path(__file__).resolve().parent / 'results' / ('telemetry_' + time.strftime('%Y%m%d_%H%M%S'))
    output.mkdir(parents=True)
    current = processes()
    originals = [(pid, args) for pid, args in current.items()
                 if '/workspace/activate/mix_module/camera_thickness.sh' in args]
    if len(originals) != 1:
        raise RuntimeError('Expected one known combined entrypoint; preserve current processes and inspect')
    print('RESULTS=' + str(output), flush=True)
    try:
        stop_tree(originals[0][0])
        time.sleep(2)
        if any(args and Path(args[0]).name == 'telemetry_bridge_node' for args in processes().values()):
            raise RuntimeError('UART still owned by telemetry node')
        measure(arguments.device, arguments.count, output)
    finally:
        restored = start_logged(['/usr/local/bin/robot-entrypoint'] + originals[0][1],
                                output / 'restored.log', os.environ.copy())
        print('RESTORED pid=' + str(restored.pid), flush=True)


if __name__ == '__main__':
    main()
