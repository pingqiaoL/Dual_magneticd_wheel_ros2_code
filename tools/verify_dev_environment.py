#!/usr/bin/env python3
"""Verify ROS imports, a real colcon C++ build, DDS delivery and MAVLink encoding."""
import datetime
import importlib.metadata
import json
import os
from pathlib import Path
import platform
import subprocess
import tempfile
import time
import uuid


def main():
    if os.environ.get("ROS_DISTRO") != "jazzy":
        raise RuntimeError("请在已加载 ROS Jazzy 的开发容器中执行")

    import rclpy
    from std_msgs.msg import String
    from pymavlink.dialects.v20 import common as mavlink
    import serial

    token = "ros2_environment_" + uuid.uuid4().hex
    topic = "/" + token
    result = {
        "checked_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "architecture": platform.machine(),
        "ros_distro": os.environ["ROS_DISTRO"],
        "rmw": os.environ.get("RMW_IMPLEMENTATION"),
        "python": platform.python_version(),
        "pymavlink": importlib.metadata.version("pymavlink"),
        "pyserial": serial.__version__,
        "colcon_cpp_build": False,
        "dds_delivery": False,
        "mavlink2_roundtrip": False,
        "hardware_validated": False,
    }

    with tempfile.TemporaryDirectory(prefix="robot-ros2-check-") as directory:
        workspace = Path(directory)
        package = workspace / "src/environment_smoke"
        package.mkdir(parents=True)
        (package / "package.xml").write_text('''<?xml version="1.0"?>
<package format="3">
  <name>environment_smoke</name><version>0.0.1</version>
  <description>Temporary environment verification</description>
  <maintainer email="test@example.invalid">Environment test</maintainer>
  <license>Apache-2.0</license>
  <buildtool_depend>ament_cmake</buildtool_depend>
  <depend>rclcpp</depend><depend>std_msgs</depend>
  <export><build_type>ament_cmake</build_type></export>
</package>
''')
        (package / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.16)
project(environment_smoke)
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
add_executable(smoke_publisher publisher.cpp)
ament_target_dependencies(smoke_publisher rclcpp std_msgs)
install(TARGETS smoke_publisher DESTINATION lib/${PROJECT_NAME})
ament_package()
''')
        (package / "publisher.cpp").write_text('''#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <chrono>
#include <memory>
int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<rclcpp::Node>("environment_smoke_publisher");
  auto publisher = node->create_publisher<std_msgs::msg::String>("TOPIC", 10);
  std_msgs::msg::String message;
  message.data = "TOKEN";
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
  rclcpp::WallRate rate(10);
  while (rclcpp::ok() && std::chrono::steady_clock::now() < end) {
    publisher->publish(message);
    rclcpp::spin_some(node);
    rate.sleep();
  }
  rclcpp::shutdown();
  return 0;
}
'''.replace("TOPIC", topic).replace("TOKEN", token))
        subprocess.run([
            "colcon", "--log-base", str(workspace / "log"), "build",
            "--base-paths", str(workspace / "src"),
            "--build-base", str(workspace / "build"),
            "--install-base", str(workspace / "install"),
            "--event-handlers", "console_direct+",
        ], cwd=workspace, check=True, timeout=180)
        result["colcon_cpp_build"] = True

        received = []
        rclpy.init()
        node = rclpy.create_node("environment_smoke_receiver")
        subscription = node.create_subscription(String, topic, lambda message: received.append(message.data), 10)
        publisher = None
        try:
            publisher = subprocess.Popen([
                str(workspace / "install/environment_smoke/lib/environment_smoke/smoke_publisher")
            ], stdout=subprocess.DEVNULL)
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline and token not in received:
                rclpy.spin_once(node, timeout_sec=0.25)
            if token not in received:
                raise RuntimeError("C++ 发布到 Python 订阅的 DDS 验证超时")
            result["dds_delivery"] = True
        finally:
            if publisher is not None and publisher.poll() is None:
                publisher.terminate()
                try:
                    publisher.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    publisher.kill()
                    publisher.wait(timeout=5)
            node.destroy_subscription(subscription)
            node.destroy_node()
            rclpy.shutdown()

    encoder = mavlink.MAVLink(None, srcSystem=1, srcComponent=191)
    message = mavlink.MAVLink_heartbeat_message(
        mavlink.MAV_TYPE_ONBOARD_CONTROLLER,
        mavlink.MAV_AUTOPILOT_INVALID, 0, 0, mavlink.MAV_STATE_ACTIVE, 3,
    )
    packet = message.pack(encoder)
    decoded = mavlink.MAVLink(None).parse_char(packet)
    if not packet or packet[0] != 0xFD or decoded is None or decoded.get_type() != "HEARTBEAT":
        raise RuntimeError("MAVLink 2 HEARTBEAT 编解码验证失败")
    result["mavlink2_roundtrip"] = True
    report = Path("/workspace/log/environment-verification.json")
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    print("基础开发环境验证通过；未进行真实串口、电机、测厚仪或视频验证。")


if __name__ == "__main__":
    main()
