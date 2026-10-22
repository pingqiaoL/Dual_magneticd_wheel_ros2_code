# camera_driver

实现状态：已实现并完成真实摄像头本机回环验证，2026-10-19 工作区未提交。

CameraCaptureNode 是可组合节点，唯一打开摄像头并发布 /camera/image_raw、/camera/camera_info 和 /camera/status。CameraDevice 使用 GStreamer/V4L2 采集，保持独立线程及错误重试，输出紧凑 bgr8。CameraInfo 未标定。

通过 video_streamer_transmission 的 XML 统一启动；也可以 `ros2 run camera_driver camera_capture_node` 独立运行。source_mode=test 必须显式设置。当前真实摄像头默认模式为 MJPEG 1280×720@30。

完整设计见 [视频模块](../../docs/modules/视频模块.md)，操作见 [联调指南](../../docs/guides/摄像头与视频联调.md)。test_camera_device 使用测试源和不存在的设备路径，不打开真实摄像头。
