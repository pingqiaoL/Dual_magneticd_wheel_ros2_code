#!/usr/bin/env python3
"""Read MK32 MAVLink and optionally send only link heartbeat/PING replies."""

import argparse
import json
import os
import select
import termios
import time

from pymavlink.dialects.v20 import common as mavlink


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("device", nargs="?", default="/dev/ttyTHS0")
    parser.add_argument("--duration", type=float, default=15)
    parser.add_argument("--handshake", action="store_true")
    parser.add_argument("--capture", help="optional raw input log path")
    parser.add_argument("--include-bad-data", action="store_true")
    arguments = parser.parse_args()
    descriptor = os.open(arguments.device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    previous = termios.tcgetattr(descriptor)
    previous = [*previous[:6], previous[6][:]]
    config = termios.tcgetattr(descriptor)
    config[0] = 0
    config[1] = 0
    config[2] = termios.CS8 | termios.CLOCAL | termios.CREAD
    config[3] = 0
    config[4] = termios.B115200
    config[5] = termios.B115200
    config[6][termios.VMIN] = 0
    config[6][termios.VTIME] = 0
    termios.tcsetattr(descriptor, termios.TCSANOW, config)

    class Writer:
        def write(self, data):
            offset = 0
            deadline = time.monotonic() + 0.1
            while offset < len(data):
                if time.monotonic() >= deadline:
                    raise TimeoutError("serial write timeout")
                if select.select([], [descriptor], [], 0.01)[1]:
                    offset += os.write(descriptor, data[offset:])
            return offset

    endpoint = mavlink.MAVLink(Writer(), srcSystem=1, srcComponent=191)
    endpoint.robust_parsing = True
    received_bytes = 0
    received_messages = {}
    raw_capture = bytearray()
    next_heartbeat = 0
    deadline = time.monotonic() + arguments.duration
    try:
        while time.monotonic() < deadline:
            current = time.monotonic()
            if arguments.handshake and current >= next_heartbeat:
                endpoint.heartbeat_send(mavlink.MAV_TYPE_ONBOARD_CONTROLLER,
                                        mavlink.MAV_AUTOPILOT_INVALID, 0, 0,
                                        mavlink.MAV_STATE_STANDBY)
                next_heartbeat = current + 1
            if not select.select([descriptor], [], [], 0.05)[0]:
                continue
            data = os.read(descriptor, 4096)
            raw_capture.extend(data)
            received_bytes += len(data)
            for byte in data:
                message = endpoint.parse_char(bytes([byte]))
                if message is None:
                    continue
                name = message.get_type()
                received_messages[name] = received_messages.get(name, 0) + 1
                if name == "BAD_DATA":
                    if arguments.include_bad_data:
                        print(json.dumps({"bad_data": bytes(message.data).hex(),
                                          "reason": message.reason}), flush=True)
                    continue
                print(json.dumps({"source_system": message.get_srcSystem(),
                                  "source_component": message.get_srcComponent(),
                                  "version": message.get_msgbuf()[0],
                                  "message": message.to_dict()}), flush=True)
                if (arguments.handshake and name == "PING" and message.target_system == 0
                        and message.target_component == 0):
                    endpoint.ping_send(message.time_usec, message.seq, message.get_srcSystem(),
                                       message.get_srcComponent())
        print(json.dumps({"device": arguments.device, "baud_rate": 115200,
                          "received_bytes": received_bytes, "messages": received_messages,
                          "handshake": arguments.handshake}), flush=True)
    finally:
        if arguments.capture:
            with open(arguments.capture, "wb") as capture:
                capture.write(raw_capture)
        termios.tcsetattr(descriptor, termios.TCSANOW, previous)
        os.close(descriptor)


if __name__ == "__main__":
    main()
