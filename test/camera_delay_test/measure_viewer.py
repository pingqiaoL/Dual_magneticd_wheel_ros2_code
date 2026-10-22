#!/usr/bin/env python3
"""Measure ROS arrival and GUI calls; gui_end is NOT physical presentation."""

import argparse
import csv
import threading
import time
from pathlib import Path

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy
from sensor_msgs.msg import Image


class MeasuredViewer(Node):
    def __init__(self, mode, depth):
        super().__init__('camera_delay_viewer')
        self.mode = mode
        self.events = []
        self.latest = None
        self.last_displayed_id = None
        self.lock = threading.Lock()
        self.should_exit = False
        self.window = 'Camera delay test - ' + mode
        if mode != 'headless':
            import cv2
            from cv_bridge import CvBridge
            self.cv2 = cv2
            self.bridge = CvBridge()
            cv2.namedWindow(self.window, cv2.WINDOW_NORMAL)
        qos = QoSProfile(history=HistoryPolicy.KEEP_LAST, depth=depth,
                         reliability=ReliabilityPolicy.BEST_EFFORT)
        self.subscription = self.create_subscription(Image, '/camera/image_raw', self.on_image, qos)

    def event(self, stage, frame_id):
        self.events.append((time.monotonic_ns(), stage, frame_id, 0))

    def on_image(self, message):
        frame_id = message.header.stamp.sec * 1000000000 + message.header.stamp.nanosec
        self.event('receive', frame_id)
        if self.mode == 'headless':
            return
        if self.mode == 'legacy':
            self.display(message, frame_id)
        else:
            with self.lock:
                self.latest = (message, frame_id)

    def display(self, message, frame_id):
        self.event('display_start', frame_id)
        frame = self.bridge.imgmsg_to_cv2(message, desired_encoding='bgr8')
        self.event('convert_end', frame_id)
        self.cv2.putText(frame, f'{self.mode} {message.width}x{message.height}',
                         (20, 40), self.cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 255, 0), 2)
        self.event('draw_end', frame_id)
        self.cv2.imshow(self.window, frame)
        key = self.cv2.waitKey(1) & 0xFF
        self.event('gui_end', frame_id)
        self.should_exit = key in (ord('q'), 27)
        self.last_displayed_id = frame_id


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mode', choices=['headless', 'legacy', 'latest'], required=True)
    parser.add_argument('--depth', type=int, required=True)
    parser.add_argument('--duration', type=float, default=70)
    parser.add_argument('--output', type=Path, required=True)
    arguments = parser.parse_args()
    rclpy.init()
    node = MeasuredViewer(arguments.mode, arguments.depth)
    finished = threading.Event()
    worker = None
    try:
        if arguments.mode == 'latest':
            def receive_loop():
                while not finished.is_set() and rclpy.ok():
                    rclpy.spin_once(node, timeout_sec=0.02)
            worker = threading.Thread(target=receive_loop)
            worker.start()
        deadline = time.monotonic() + arguments.duration
        next_report = time.monotonic() + 1
        while time.monotonic() < deadline and not node.should_exit and rclpy.ok():
            if arguments.mode == 'latest':
                with node.lock:
                    latest = node.latest
                if latest and latest[1] != node.last_displayed_id:
                    node.display(*latest)
                else:
                    node.cv2.waitKey(1)
            else:
                rclpy.spin_once(node, timeout_sec=0.02)
            if time.monotonic() >= next_report:
                print(f'mode={arguments.mode} events={len(node.events)}', flush=True)
                next_report += 1
    except KeyboardInterrupt:
        pass
    finally:
        finished.set()
        if worker:
            worker.join(timeout=2)
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        with arguments.output.open('w', newline='') as stream:
            csv.writer(stream).writerows(node.events)
        if arguments.mode != 'headless':
            node.cv2.destroyAllWindows()
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()
