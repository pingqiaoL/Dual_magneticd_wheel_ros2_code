# SIYI FPV 黑屏排查与兼容测试

日期：2026-10-23。遥控端 SIYI FPV：2.5.15.103.d9225e2。

## 结论

已在遥控器截图中确认真实摄像头画面正常显示。当前使用独立测试链路，没有修改正式摄像头模块或安卓应用源码。

测试地址：`rtsp://192.168.144.129:18554/live`。

## 实测过程

| 对照 | 结果 |
| --- | --- |
| 正式 8554 服务直接接 FPV | OPTIONS、DESCRIBE、SETUP 成功；PLAY 返回 400，随后出现大量 PAUSE/455 |
| 只修正 PLAY 请求，保留正式视频编码 | 已收到 RTP；仍黑屏，HardDecoder 反复重置 |
| 测试图案，720p，单线程编码 | 显示正常 |
| 测试彩条，1080p，threads=2、sliced-threads=true | 画面异常，截图为绿色 |
| 同一彩条测试改 threads=1、sliced-threads=false | 彩条正常 |
| 订阅真实 /camera/image_yuv，单线程、不分片 | 真实摄像头显示正常，推送日志持续 GST_FLOW_OK |

PLAY 请求携带 `Range: 0-`，GStreamer 服务返回 400。测试代理仅移除这个非法 Range，不修改媒体内容。编码对照证明双线程分片组合存在 FPV 播放兼容问题；尚未单独拆分线程数与分片参数两个变量，不应推断所有多线程编码都不兼容。

## 测试链路

已有摄像头采集 → ROS /camera/image_yuv → camera_rtsp.py 单线程 H.264 → 本机 18555 → synthetic_proxy.py 请求兼容代理 18554 → SIYI FPV。

真实摄像头只采集一次。测试编码器直接读取 I420，没有把正式 H.264 解码后再压缩。测试目标配置为 1920×1080、30 FPS、6000 kbps、GOP 10、baseline、B 帧 0、zerolatency、ultrafast。未在本次测试测量端到端延迟，不应将正常出图等同于达到 200 ms。

## 文件

- `rtsp_compat.py`：18554 到正式 8554 的请求兼容代理，用于第一阶段排查。
- `synthetic_rtsp.py`、`synthetic_original.py`：测试图案服务及最初配置。
- `synthetic_proxy.py`：18554 到测试 18555 的兼容代理，当前真实摄像头测试沿用它。日志开头端口说明沿用旧文字，以代码 upstream 18555 为准。
- `camera_rtsp.py`：订阅真实 I420 并提供测试 RTSP；同一时刻与 synthetic_rtsp.py 二选一。
- `capture_rtsp.py`：18 秒抓包工具，需要 root；日志含二进制转义，体积较大。
- `*.log`：请求、错误与推送证据。

容器内启动真实摄像头测试（要求正式摄像头采集已运行，端口无冲突）：

```bash
source /opt/ros/jazzy/setup.bash
source /workspace/install/setup.bash
python3 /workspace/test/camera_delay_optimize/fpv_compat/camera_rtsp.py
```

另一终端运行：

```bash
python3 /workspace/test/camera_delay_optimize/fpv_compat/synthetic_proxy.py
```

本次通过 docker exec -d 启动以上两个进程，供现场观察。结束时只停止对应测试 Python 进程，避免按 python3 批量结束其他模块。正式 8554 视频服务、测厚与数传不在测试脚本修改范围。

## 后续迁移建议

若确定长期使用 SIYI FPV，给正式服务增加显式的兼容编码配置，并在请求入口规范化该客户端的 Range。先保留自研 App 原配置，再比较帧率、CPU、运动画质和端到端延迟。本次仅完成兼容性验证，没有将实验参数直接替换到正式版本。
