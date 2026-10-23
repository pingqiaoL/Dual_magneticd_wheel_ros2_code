# video_streamer_transmission

VideoStreamerNode 订阅 `/camera/image_yuv`，检查 I420 布局与帧年龄后提交最新帧槽。工作线程按30 FPS取最新帧，x264编码并提供共享RTSP服务。输入不经BGR转换，不重复发送旧帧。

默认1920×1080、30 FPS、6000 kbit/s、GOP 10、baseline、zerolatency、ultrafast、无B帧、2个切片线程；采集默认60 FPS。appsrc和编码前队列各保留1帧。`max_frame_age_ms=150`不等于端到端延迟上限。

```bash
bash /workspace/activate/single_module/camera.sh
bash /workspace/activate/internal/stop_video.sh
```

`/video/set_enabled`只控制RTSP服务，完整停止需运行停止脚本。正式运行不再依赖test构建、计时探针或CAMERA_DELAY环境变量。

布局与验证边界见 [视频模块](../../docs/modules/视频模块.md)，命令见 [模块测试](../../docs/module_test.md)。
