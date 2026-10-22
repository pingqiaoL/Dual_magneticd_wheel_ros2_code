#!/usr/bin/env python3
"""ROS + PTY + pymavlink integration test. Never opens real robot devices."""

import math
import os
from pathlib import Path
import pty
import select
import signal
import subprocess
import time
import tty

# Isolate this test from the development robot's normal DDS domain (42).
os.environ["ROS_DOMAIN_ID"] = "79"

import rclpy
from ament_index_python.packages import get_package_prefix
from pymavlink.dialects.v20 import common as mavlink
from robot_interfaces.msg import ThicknessReading


def main():
    rclpy.init()
    node = rclpy.create_node("measurement_telemetry_integration_test")
    project_root = Path(__file__).resolve().parent.parent
    log_directory = project_root / "test" / "measurement_manager" / "results" / time.strftime("%Y%m%d_%H%M%S")
    log_directory.mkdir(parents=True, exist_ok=True)
    master_fd, slave_fd = pty.openpty()
    tty.setraw(slave_fd)
    serial_path = Path(f"/tmp/robot_telemetry_test_{os.getpid()}")
    serial_path.symlink_to(os.ttyname(slave_fd))
    os.close(slave_fd)
    os.set_blocking(master_fd, False)
    processes = []
    handles = []
    wire_messages = []
    wire_bytes = bytearray()
    processed_data = []
    parser = mavlink.MAVLink(None)
    parser.robust_parsing = True

    class SerialWriter:
        def write(self, data):
            return os.write(master_fd, data)

    remote = mavlink.MAVLink(SerialWriter(), srcSystem=255, srcComponent=190)

    def start_node(package, executable, *arguments):
        executable_path = Path(get_package_prefix(package)) / "lib" / package / executable
        handle = open(log_directory / f"{executable}_{len(processes)}.log", "w")
        handles.append(handle)
        process = subprocess.Popen([str(executable_path), *arguments], stdout=handle,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        processes.append(process)
        return process

    def pump(duration):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            rclpy.spin_once(node, timeout_sec=0.01)
            if select.select([master_fd], [], [], 0)[0]:
                try:
                    received = os.read(master_fd, 4096)
                except OSError:
                    received = b""
                wire_bytes.extend(received)
                for byte in received:
                    message = parser.parse_char(bytes([byte]))
                    if message:
                        assert message.get_type() != "BAD_DATA", "invalid MAVLink on wire"
                        wire_messages.append(message)

    def wait_until(predicate, message, timeout=5):
        deadline = time.monotonic() + timeout
        while not predicate() and time.monotonic() < deadline:
            pump(0.05)
        assert predicate(), message

    def publish(value, unit=1, finite=True, timestamp=None):
        reading = ThicknessReading()
        reading.received_at = timestamp or node.get_clock().now().to_msg()
        reading.sequence = len(processed_data) + 1
        reading.value = float(value)
        reading.unit = unit
        reading.finite = finite
        raw_publisher.publish(reading)
        pump(0.15)

    def thickness_values():
        return [message.value for message in wire_messages
                if message.get_type() == "NAMED_VALUE_FLOAT" and message.name == "THICKNESS"]

    try:
        raw_publisher = node.create_publisher(ThicknessReading, "/thickness/reading", 64)
        replay_publisher = node.create_publisher(ThicknessReading, "/measurement/thickness", 64)
        node.create_subscription(ThicknessReading, "/measurement/thickness",
                                 processed_data.append, 64)
        manager = start_node("measurement_manager", "measurement_manager_node")
        start_node("telemetry_bridge", "telemetry_bridge_node", "--ros-args", "-p",
                   f"device_path:={serial_path}")
        wait_until(lambda: raw_publisher.get_subscription_count() > 0
                   and replay_publisher.get_subscription_count() > 1,
                   "ROS nodes not discovered", timeout=10)
        pump(1.1)
        assert "/measurement/set_enabled" not in dict(node.get_service_names_and_types())
        assert "/measurement/control" not in dict(node.get_topic_names_and_types())
        publish(40)
        wait_until(lambda: thickness_values() == [40.0], "default startup output failed")
        assert len(processed_data) == 1
        processed_data.clear()
        wire_messages.clear()

        pump(1.1)
        # Driver heartbeat/PING/ACK remain independent of manager control.
        assert any(message.get_type() == "HEARTBEAT" and message.get_srcSystem() == 1 and
                   message.get_srcComponent() == 191 for message in wire_messages)
        assert any(message.get_type() == "NAMED_VALUE_INT" and message.name == "STM_LINK" and
                   message.value == 0 for message in wire_messages), \
            "application STM_LINK report missing"
        remote.ping_send(123456, 17, 0, 0)
        wait_until(lambda: any(message.get_type() == "PING" and message.seq == 17 and
                               message.time_usec == 123456 and message.target_system == 255 and
                               message.target_component == 190 for message in wire_messages),
                   "PING reply failed")
        remote.command_long_send(1, 191, 31010, 0, 0, 0, 0, 0, 0, 0, 0)
        wait_until(lambda: any(message.get_type() == "COMMAND_ACK" and message.command == 31010
                               and message.result == mavlink.MAV_RESULT_UNSUPPORTED
                               for message in wire_messages), "unsupported command ACK failed")

        for value in (49.0, 49.0, 51.0):
            publish(value)
        wait_until(lambda: len(thickness_values()) == 3, "continuous output failed")
        assert thickness_values() == [49.0, 49.0, 51.0]
        assert len(processed_data) == 3, "new measurements with equal values were suppressed"

        # Idle measurement input must not replay the last value on heartbeat/control timers.
        measurements_before_idle = list(thickness_values())
        processed_data_before_idle = len(processed_data)
        heartbeats_before_idle = sum(message.get_type() == "HEARTBEAT"
                                     for message in wire_messages)
        pump(2.2)
        assert thickness_values() == measurements_before_idle, "idle input replayed old thickness"
        assert len(processed_data) == processed_data_before_idle, \
            "idle input republished old thickness"
        assert sum(message.get_type() == "HEARTBEAT" for message in wire_messages) > \
            heartbeats_before_idle, "heartbeat stopped during idle measurement input"
        publish(math.nan)
        publish(60, finite=False)
        assert len(processed_data) == 3, "invalid sample forwarded"
        publish(61, unit=0)
        wait_until(lambda: len(thickness_values()) == 4, "legacy unit metadata blocked output")
        assert len(processed_data) == 4 and thickness_values()[-1] == 61.0

        # Delayed DDS data is stale even if it has just reached the serial node.
        stale_data = ThicknessReading()
        stale_data.received_at = node.get_clock().now().to_msg()
        stale_data.received_at.sec -= 2
        stale_data.value = 777.0
        stale_data.finite = True
        replay_publisher.publish(stale_data)
        pump(0.2)
        assert len(thickness_values()) == 4, "stale thickness reached serial wire"

        # Stop manager: no new measurements, but link heartbeat remains alive.
        os.killpg(manager.pid, signal.SIGINT)
        manager.wait(timeout=5)
        wire_before_stop = list(thickness_values())
        pump(2.3)
        assert thickness_values() == wire_before_stop, "manager stop replayed a measurement"
        assert not any(message.get_type() == "NAMED_VALUE_INT" and message.name == "MEAS_RUN"
                       for message in wire_messages), "obsolete manager state still on wire"
        start_node("measurement_manager", "measurement_manager_node")
        wait_until(lambda: raw_publisher.get_subscription_count() > 0,
                   "manager restart not discovered")
        pump(0.3)
        publish(53)
        wait_until(lambda: len(thickness_values()) == 5, "manager restart output failed")
        assert thickness_values()[-1] == 53.0

        (log_directory / "wire_capture.bin").write_bytes(wire_bytes)
        print("PASS ROS/PTTY + pymavlink: default enabled, equal-valued new readings, "
              "no idle replay, invalid values, no unit gate, stale DDS rejection, "
              "heartbeat/PING, unsupported ACK, manager stop/restart, obsolete interfaces absent")
        print(f"Wire values: {thickness_values()}; logs: {log_directory}")
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGINT)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
        for handle in handles:
            handle.close()
        serial_path.unlink(missing_ok=True)
        os.close(master_fd)
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
