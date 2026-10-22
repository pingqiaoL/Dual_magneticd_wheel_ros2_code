#!/usr/bin/env python3

"""Display a ROS 2 raw camera topic with receive-rate statistics."""

import os
import time
from pathlib import Path

import cv2
import rclpy
from cv_bridge import CvBridge, CvBridgeError
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import Image


class RawImageViewer(Node):
    """Subscribe to the raw camera topic without opening the camera device again."""

    def __init__(self) -> None:
        super().__init__("raw_image_viewer")

        self.declare_parameter("image_topic", "/camera/image_raw")
        self.declare_parameter("window_name", "ROS 2 Raw Camera")
        self.declare_parameter("screenshot_directory", "/workspace/log")

        self._image_topic = str(self.get_parameter("image_topic").value)
        self._window_name = str(self.get_parameter("window_name").value)
        self._screenshot_directory = Path(
            str(self.get_parameter("screenshot_directory").value)
        )

        if not os.environ.get("DISPLAY") and not os.environ.get("WAYLAND_DISPLAY"):
            raise RuntimeError(
                "No graphical display is available. Run this script from the NoMachine "
                "desktop and pass the display into the container."
            )

        self._bridge = CvBridge()
        self._should_exit = False
        self._frame_count = 0
        self._receive_fps = 0.0
        self._statistics_start_time = time.monotonic()
        self._latest_frame = None

        self._subscription = self.create_subscription(
            Image,
            self._image_topic,
            self._on_image,
            qos_profile_sensor_data,
        )

        cv2.namedWindow(self._window_name, cv2.WINDOW_NORMAL)
        self.get_logger().info(
            f"Viewing {self._image_topic}; press Q or Esc to exit, S to save a frame"
        )

    @property
    def should_exit(self) -> bool:
        return self._should_exit

    def _on_image(self, message: Image) -> None:
        try:
            frame = self._bridge.imgmsg_to_cv2(message, desired_encoding="bgr8")
        except CvBridgeError as error:
            self.get_logger().error(f"Image conversion failed: {error}")
            return

        current_time = time.monotonic()
        self._frame_count += 1
        statistics_duration = current_time - self._statistics_start_time

        if statistics_duration >= 1.0:
            self._receive_fps = self._frame_count / statistics_duration
            self.get_logger().info(
                f"raw_fps={self._receive_fps:.2f} "
                f"size={message.width}x{message.height} "
                f"encoding={message.encoding}"
            )
            self._frame_count = 0
            self._statistics_start_time = current_time

        self._latest_frame = frame
        overlay_text = (
            f"RAW {message.width}x{message.height}  "
            f"FPS {self._receive_fps:.1f}  {message.encoding}"
        )
        cv2.putText(
            frame,
            overlay_text,
            (20, 40),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.9,
            (0, 255, 0),
            2,
            cv2.LINE_AA,
        )
        cv2.imshow(self._window_name, frame)

        key_code = cv2.waitKey(1) & 0xFF
        if key_code in (ord("q"), 27):
            self._should_exit = True
        elif key_code == ord("s"):
            self._save_screenshot()

    def _save_screenshot(self) -> None:
        if self._latest_frame is None:
            return

        self._screenshot_directory.mkdir(parents=True, exist_ok=True)
        timestamp = time.strftime("%Y%m%d_%H%M%S")
        screenshot_path = self._screenshot_directory / f"raw_camera_{timestamp}.png"

        if cv2.imwrite(str(screenshot_path), self._latest_frame):
            self.get_logger().info(f"Saved {screenshot_path}")
        else:
            self.get_logger().error(f"Failed to save {screenshot_path}")

    def close(self) -> None:
        cv2.destroyAllWindows()


def main() -> None:
    rclpy.init()
    viewer = None

    try:
        viewer = RawImageViewer()
        while rclpy.ok() and not viewer.should_exit:
            rclpy.spin_once(viewer, timeout_sec=0.1)
    except KeyboardInterrupt:
        pass
    except (RuntimeError, cv2.error) as error:
        if viewer is not None:
            viewer.get_logger().error(str(error))
        else:
            print(f"raw image viewer failed: {error}")
    finally:
        if viewer is not None:
            viewer.close()
            viewer.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == "__main__":
    main()
