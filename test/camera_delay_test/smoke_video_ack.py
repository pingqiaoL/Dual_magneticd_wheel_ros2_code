#!/usr/bin/env python3
"""Synthetic source on port 8555; validates trace mapping, not wireless latency."""

import csv
import os
import time
from pathlib import Path

from pymavlink.dialects.v20 import common as mavlink
from run_matrix import start_logged, stop_tree

suite = Path(__file__).resolve().parent
output = suite / 'results' / ('rtp_smoke_' + str(time.monotonic_ns()))
output.mkdir(parents=True)
environment = os.environ.copy()
environment.update(ROS_DOMAIN_ID='79', CAMERA_DELAY_OUTPUT=str(output))
command = ('source /workspace/log/camera_delay_build_20261020/install/setup.bash && '
           'exec ros2 launch video_streamer_transmission video.launch.xml '
           'source_mode:=test rtsp_port:=8555 width:=640 height:=480 bitrate_kbps:=2000')
children = []
try:
    launch = start_logged(['bash', '-c', command], output / 'video.log', environment)
    children.append(launch)
    deadline = time.monotonic() + 20
    while 'RTSP :8555/live' not in (output / 'video.log').read_text():
        if launch.poll() is not None or time.monotonic() > deadline:
            raise RuntimeError((output / 'video.log').read_text())
        time.sleep(0.1)
    receiver = start_logged(['gst-launch-1.0', '-q', 'rtspsrc',
                            'location=rtsp://127.0.0.1:8555/live', 'protocols=udp', 'latency=0',
                            '!', 'rtph264depay', '!', 'fakesink', 'sync=false'],
                           output / 'receiver.log', environment)
    children.append(receiver)
    time.sleep(6)
    assert receiver.poll() is None, (output / 'receiver.log').read_text()
finally:
    for child in reversed(children):
        if child.poll() is None:
            stop_tree(child.pid)
            child.wait(timeout=20)
encoded, first, last = {}, {}, {}
for path in output.glob('trace_*.csv'):
    with path.open() as stream:
        for timestamp, stage, frame_id, related_id in csv.reader(stream):
            if stage == 'encoded':
                encoded[int(frame_id)] = int(timestamp)
            elif stage == 'rtp_first':
                first[int(frame_id)] = (int(timestamp), int(related_id))
            elif stage == 'rtp_end':
                last[int(frame_id)] = (int(timestamp), int(related_id))
matched = [key for key in last if last[key][1] in encoded and key in first]
print(dict(encoded=len(encoded), first=len(first), last=len(last), matched=len(matched), output=str(output)))
assert len(matched) >= 30 and len(matched) / len(last) >= 0.95
decoder = mavlink.MAVLink(None)
packet = bytes.fromhex('fd0e000007ffbe04000078563412bebafeca7b00008001bf0958')
message = None
for value in packet:
    message = decoder.parse_char(bytes([value])) or message
assert message and message.get_type() == 'PING'
assert message.time_usec == 0xcafebabe12345678 and message.seq == 0x8000007b
assert message.target_system == 1 and message.target_component == 191
print('PASS RTP exact frame mapping and Java-to-pymavlink ACK CRC/fields')
