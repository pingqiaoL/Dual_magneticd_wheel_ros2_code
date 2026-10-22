#!/usr/bin/env python3
"""Passive IPv4 header inspection, no payload saved and no network changes."""
import json
import socket
import struct
import time
from pathlib import Path

packets = []
with socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(0x0003)) as capture:
    capture.bind(('eth0', 0))
    capture.settimeout(.5)
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline and len(packets) < 30:
        try:
            packet = capture.recv(65535)
        except socket.timeout:
            continue
        if len(packet) < 42 or packet[12:14] != b'\x08\x00':
            continue
        source = socket.inet_ntoa(packet[26:30])
        destination = socket.inet_ntoa(packet[30:34])
        offset = 14 + (packet[14] & 15) * 4
        if source != '192.168.144.129' or destination != '192.168.144.20' or packet[23] != 17:
            continue
        if len(packet) < offset + 8:
            continue
        source_port, destination_port, length = struct.unpack('!HHH', packet[offset:offset + 6])
        packets.append(dict(source=source, destination=destination, source_port=source_port,
                            destination_port=destination_port, udp_length=length))
result = dict(interface='eth0', outgoing_udp_headers=packets, saved_payload=False)
output = Path(__file__).resolve().parent / 'results/transport.json'
output.write_text(json.dumps(result, indent=2))
print(json.dumps(result, indent=2))
