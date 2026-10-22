# video_streamer_transmission

实现状态：已实现并完成真实摄像头本机回环验证，2026-10-19 工作区未提交。

VideoStreamerNode 订阅 /camera/image_raw，只接收配置尺寸的紧凑 bgr8 新图像。视频工作线程通过有界队列执行 x264enc 软件编码，提供共享 RTSP 服务；ROS 回调不执行编码。默认使用 1280×720、30 FPS、6000 kbit/s、veryfast 和 zerolatency，端口 8554，路径 /live。

```bash
bash /workspace/activate/single_module/camera.sh
```

XML 默认将采集和视频两个组件加载到同一进程并开启进程内通信。也可 `ros2 run video_streamer_transmission video_streamer_node` 单独运行，此时需独立启动图像发布者。/video/set_enabled 只控制 RTSP 服务，不停止相机。

完整设计见 [视频模块](../../docs/modules/视频模块.md)，操作和测试见 [联调指南](../../docs/guides/摄像头与视频联调.md)。本机回环通过不等于天空端到安卓播放通过。视觉算法与录像尚未实现。

2026-10-20 延迟优化：appsrc 与编码前原始帧队列各保留 1 帧，过载丢旧；最大帧年龄默认 150 ms，分别检查 ROS 入口消息年龄与工作线程等待。该门限不代表曝光到显示的总延迟上限。实测与回滚位置见 `test/camera_delay/OPTIMIZATION.md`。
