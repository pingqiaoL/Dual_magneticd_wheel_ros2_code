#!/usr/bin/env python3
"""Host-side RTSP/network precheck. ICMP RTT is not video latency."""

import argparse
import ipaddress
import json
import socket
import subprocess
import time
from datetime import datetime, timedelta, timezone
from pathlib import Path


def command(arguments):
    result = subprocess.run(arguments, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=15)
    return dict(returncode=result.returncode, output=result.stdout)


def video_connections():
    connections = []
    for row in Path('/proc/net/tcp').read_text().splitlines()[1:]:
        fields = row.split()
        address, port = fields[1].split(':')
        if int(port, 16) != 8554 or fields[3] != '01':
            continue
        peer_address, peer_port = fields[2].split(':')
        peer_ip = socket.inet_ntoa(bytes.fromhex(peer_address)[::-1])
        connections.append(dict(peer_ip=peer_ip, peer_port=int(peer_port, 16),
                                send_queue_bytes=int(fields[4].split(':')[0], 16)))
    return connections


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--android-ip', type=ipaddress.IPv4Address, default=ipaddress.IPv4Address('192.168.144.20'))
    parser.add_argument('--ground-ip', type=ipaddress.IPv4Address, default=ipaddress.IPv4Address('192.168.144.12'))
    arguments = parser.parse_args()
    timestamp = datetime.now(timezone(timedelta(hours=8)))
    output = Path(__file__).resolve().parent / 'results' / ('video_precheck_' + timestamp.strftime('%Y%m%d_%H%M%S'))
    output.mkdir(parents=True)
    result = dict(time=timestamp.isoformat(),
                  existing_rtsp_connections=video_connections(),
                  interfaces=command(['ip', '-br', 'address']))
    try:
        with socket.create_connection(('127.0.0.1', 8554), timeout=3) as connection:
            connection.sendall(b'OPTIONS rtsp://127.0.0.1:8554/live RTSP/1.0\r\nCSeq: 1\r\n\r\n')
            result['rtsp_options_status'] = connection.recv(4096).decode(errors='replace').splitlines()[0]
    except (OSError, IndexError) as error:
        result['rtsp_options_error'] = str(error)
    for name, address in [('ground', arguments.ground_ip), ('android', arguments.android_ip)]:
        result[name + '_icmp'] = command(['ping', '-n', '-c', '3', '-W', '1', str(address)])
        result[name + '_neighbor'] = command(['ip', 'neigh', 'show', 'to', str(address)])
    result['limitations'] = ('No per-frame Android ACK available. ICMP RTT and RTSP OPTIONS response '
                            'do not measure video reception, decoding or rendering latency.')
    (output / 'summary.json').write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding='utf-8')
    print(json.dumps(result, indent=2, ensure_ascii=False))
    print('RESULTS=' + str(output))


if __name__ == '__main__':
    main()
