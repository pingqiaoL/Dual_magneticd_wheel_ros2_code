#!/usr/bin/env python3
"""Finite ROS/composition/RTSP decode test. Hardware use requires --hardware."""
import argparse
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--hardware", action="store_true")
arguments = parser.parse_args()
os.environ["ROS_DOMAIN_ID"] = "81"

import gi
gi.require_version("Gst", "1.0")
from gi.repository import Gst
import rclpy
from diagnostic_msgs.msg import DiagnosticArray
from sensor_msgs.msg import Image, CameraInfo
from rclpy.qos import qos_profile_sensor_data
from std_srvs.srv import SetBool
from composition_interfaces.srv import ListNodes, UnloadNode


def main():
    Gst.init(None)
    rclpy.init()
    node = rclpy.create_node("video_pipeline_integration_test")
    logs = Path("/workspace/log/video_20261019")
    logs.mkdir(parents=True, exist_ok=True)
    mode = "hardware" if arguments.hardware else "synthetic"
    width, height, fps = (1920, 1080, 30) if arguments.hardware else (320, 240, 15)
    capture_fps = 60 if arguments.hardware else fps
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    status = {}
    raw_frames = []
    yuv_frames = []
    info_messages = []
    receivers = []
    decoded_counts = {}
    received_samples = []
    handle = open(logs / f"{mode}_launch.log", "w")
    process = subprocess.Popen([
        "ros2", "launch", "video_streamer_transmission", "video.launch.xml",
        f"source_mode:={'camera' if arguments.hardware else 'test'}",
        f"width:={width}", f"height:={height}", f"frame_rate:={fps}", f"capture_frame_rate:={capture_fps}",
        f"rtsp_port:={port}"], stdout=handle, stderr=subprocess.STDOUT, start_new_session=True)

    def receive_status(message):
        for item in message.status:
            status[item.name] = (item.message, {value.key: value.value for value in item.values})

    node.create_subscription(DiagnosticArray, "/camera/status", receive_status, 1)
    node.create_subscription(DiagnosticArray, "/video/status", receive_status, 1)
    # 第二路图像消费者模拟后续视觉节点；只保存计数和布局，不囤积图像。
    node.create_subscription(Image, "/camera/image_raw",
        lambda image: raw_frames.append((image.width, image.height, image.encoding, len(image.data))),
        qos_profile_sensor_data)
    node.create_subscription(Image, "/camera/image_yuv",
        lambda image: yuv_frames.append((image.width, image.height, image.encoding, len(image.data))),
        qos_profile_sensor_data)
    node.create_subscription(CameraInfo, "/camera/camera_info", info_messages.append,
                              qos_profile_sensor_data)

    def spin(duration):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            assert process.poll() is None, f"launch exited; see {handle.name}"
            rclpy.spin_once(node, timeout_sec=0.02)
            for pipeline in receivers:
                sink = pipeline.get_by_name("frames")
                sample = sink.emit("try-pull-sample", 0)
                if sample:
                    decoded_counts[pipeline] += 1
                    received_samples.append(sample)
                    if len(received_samples) > 20:
                        received_samples.pop(0)

    def wait(predicate, reason, timeout=15):
        deadline = time.monotonic() + timeout
        while not predicate() and time.monotonic() < deadline:
            spin(0.05)
        if not predicate():
            raise AssertionError(f"{reason}; status={status}; log={handle.name}")

    def receiver():
        pipeline = Gst.parse_launch(
            f'rtspsrc location=rtsp://127.0.0.1:{port}/live protocols=tcp latency=100 ! '
            'rtph264depay ! h264parse ! avdec_h264 ! videoconvert ! '
            'video/x-raw,format=BGR ! appsink name=frames max-buffers=2 drop=true sync=false')
        receivers.append(pipeline)
        decoded_counts[pipeline] = 0
        pipeline.set_state(Gst.State.PLAYING)
        return pipeline

    def service(client, request):
        wait(client.service_is_ready, "service not discovered")
        future = client.call_async(request)
        wait(future.done, "service timeout", timeout=10)
        return future.result()

    def enable(value, expected_success=True):
        request = SetBool.Request()
        request.data = value
        response = service(enable_client, request)
        assert response.success == expected_success, response.message

    try:
        wait(lambda: "camera_capture" in status and "video_stream" in status and len(raw_frames) >= 5,
             "capture/composition did not start")
        assert status["video_stream"][0] == "LISTENING"
        assert raw_frames[-1] == (width, height, "bgr8", width * height * 3)
        assert yuv_frames[-1] == (width, height, "yuv420", width * height * 3 // 2)
        assert info_messages and info_messages[-1].k[0] == 0.0
        enable_client = node.create_client(SetBool, "/video/set_enabled")
        first_receiver = receiver()
        wait(lambda: decoded_counts[first_receiver] >= 5, "RTSP H264 decode failed")
        sample = received_samples[-1]
        structure = sample.get_caps().get_structure(0)
        assert structure.get_value("width") == width and structure.get_value("height") == height
        assert sample.get_buffer().get_size() >= width * height * 3
        # 保存一张解码后的真实/模拟帧，明确标注来源。
        png = Gst.parse_launch(
            f'appsrc name=source format=time caps=video/x-raw,format=BGR,width={width},height={height},framerate=1/1 ! '
            f'videoconvert ! pngenc snapshot=true ! filesink location={logs}/{mode}_decoded.png')
        png.set_state(Gst.State.PLAYING)
        png.get_by_name("source").emit("push-buffer", sample.get_buffer().copy_deep())
        png.get_by_name("source").emit("end-of-stream")
        png.get_bus().timed_pop_filtered(3 * Gst.SECOND, Gst.MessageType.EOS | Gst.MessageType.ERROR)
        png.set_state(Gst.State.NULL)
        first_encoded = int(status["video_stream"][1].get("encoded_frames", 0))
        second_receiver = receiver()
        wait(lambda: status["video_stream"][0] == "STREAMING" and
             int(status["video_stream"][1]["client_count"]) >= 2 and
             decoded_counts[second_receiver] >= 3, "two RTSP clients failed")
        spin(2)
        assert int(status["video_stream"][1]["encoded_frames"]) > first_encoded
        enable(False)
        for pipeline in receivers:
            pipeline.set_state(Gst.State.NULL)
        receivers.clear()
        frames_before = len(raw_frames)
        wait(lambda: status["video_stream"][0] == "STOPPED", "stop not reported")
        spin(0.5)
        assert len(raw_frames) > frames_before, "video stop incorrectly stopped camera"
        # 占用监听端口，验证启动失败可明确反馈，并能在端口释放后恢复。
        with socket.socket() as blocker:
            blocker.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            blocker.bind(("0.0.0.0", port))
            blocker.listen(1)
            enable(True, expected_success=False)
        enable(True)
        received_samples.clear()
        receiver()
        wait(lambda: len(received_samples) >= 3, "RTSP restart/reconnect failed")

        # 卸载采集组件模拟上游停止，视频端不能循环重发最后一帧。
        list_client = node.create_client(ListNodes, "/video_container/_container/list_nodes")
        loaded = service(list_client, ListNodes.Request())
        capture_id = next(identity for name, identity in zip(loaded.full_node_names, loaded.unique_ids)
                          if name.endswith("/camera_capture_node"))
        unload = UnloadNode.Request()
        unload.unique_id = capture_id
        unload_client = node.create_client(UnloadNode, "/video_container/_container/unload_node")
        assert service(unload_client, unload).success
        wait(lambda: status["video_stream"][0] == "WAITING_FRAME", "missing input not detected")
        before_idle = int(status["video_stream"][1]["encoded_frames"])
        spin(1.2)
        assert int(status["video_stream"][1]["encoded_frames"]) == before_idle, "last frame replayed"
        publisher = node.create_publisher(Image, "/camera/image_yuv", qos_profile_sensor_data)
        wait(lambda: publisher.get_subscription_count() > 0, "image subscription missing")
        invalid = Image()
        invalid.header.stamp = node.get_clock().now().to_msg()
        invalid.width, invalid.height, invalid.encoding = width, height, "yuv420"
        invalid.step = width
        publisher.publish(invalid)
        stale = Image()
        stale.header.stamp = node.get_clock().now().to_msg()
        stale.header.stamp.sec -= 2
        stale.width, stale.height, stale.encoding = width, height, "yuv420"
        stale.step = width
        stale.data = bytes(width * height * 3 // 2)
        publisher.publish(stale)
        spin(1.1)
        assert int(status["video_stream"][1]["encoded_frames"]) == before_idle, "invalid image encoded"
        report = {"mode": mode, "width": width, "height": height, "target_fps": fps, "rtsp_port": port,
                  "raw_frames_received": len(raw_frames), "status": status,
                  "decoded_frames_by_receiver": {pipeline.get_name(): count for pipeline, count in decoded_counts.items()},
                  "checks": ["composition", "I420_and_BGR_branches", "second_image_consumer", "H264_RTSP_decode",
                             "two_clients", "stop_keeps_capture", "port_conflict_recovery", "restart", "input_timeout",
                             "no_stale_frame_replay", "invalid_and_stale_image_rejected"]}
    finally:
        for pipeline in receivers:
            pipeline.set_state(Gst.State.NULL)
        if process.poll() is None:
            process.send_signal(signal.SIGINT)
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
        handle.close()
        with socket.socket() as stopped_probe:
            stopped_probe.settimeout(1)
            assert stopped_probe.connect_ex(("127.0.0.1", port)) != 0, "RTSP port still open after exit"
        node.destroy_node()
        rclpy.shutdown()
    report["checks"].append("exit_releases_port")
    (logs / f"{mode}_report.json").write_text(json.dumps(report, indent=2))
    print("PASS", json.dumps(report))


if __name__ == "__main__":
    main()
