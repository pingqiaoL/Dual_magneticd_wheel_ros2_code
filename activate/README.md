# 启动脚本

所有入口均可在容器内运行，也支持从 Orin 宿主机运行并自动进入开发容器。以下示例统一使用容器路径；宿主机对应路径为 `/home/nvidia/robot_ros2/activate/`。

## 入口

| 文件 | 启动内容 | 接受参数 |
|---|---|---|
| `robot.sh` | 当前整机功能，调用组合入口 | 下表全部参数 |
| `single_module/camera.sh` | 摄像头采集 + RTSP 视频传输 | `video_*` |
| `single_module/thickness.sh` | 测厚采集 + manager + 可选数传 | `thickness_*`、`telemetry_*` |
| `mix_module/camera_thickness.sh` | 同时运行上述两个模块 | 下表全部参数 |
| `internal/stop_video.sh` | 停止本工程当前视频 launch | 无，供入口内部调用 |
| `internal/stop_launch_tree.sh` | 按 PID 树结束本次进程 | 内部使用，不作为用户入口 |

正常测试步骤见 [模块测试](../docs/module_test.md)。启动脚本不自动编译。

## 参数

写法为 `名称:=值`。单模块、组合和总入口使用同一套名称；未指定时使用默认值。

| 参数 | 默认值 | 含义 / 可选值 |
|---|---|---|
| `video_source_mode` | `camera` | `camera` 为真实设备；`test` 为合成测试图，无需摄像头输入 |
| `video_device_path` | `/dev/video0` | 容器内摄像头路径 |
| `video_input_format` | `MJPEG` | 输入格式：`MJPEG`、`YUYV`、`H264`，须设备支持 |
| `video_width` | `1920` | 图像宽度，像素 |
| `video_height` | `1080` | 图像高度，像素 |
| `video_capture_frame_rate` | `60` | 摄像头采集帧率，FPS，须设备支持 |
| `video_frame_rate` | `30` | 编码输出目标帧率，FPS |
| `video_keyframe_interval_frames` | `10` | 最大关键帧间隔，编码帧数 |
| `video_bitrate_kbps` | `6000` | H.264 目标码率，kbit/s |
| `video_rtsp_port` | `8554` | RTSP TCP 监听端口 |
| `thickness_device_path` | `/dev/ttyACM0` | 容器内测厚仪路径 |
| `thickness_baud_rate` | `115200` | 测厚串口线路波特率 |
| `telemetry_enabled` | `true` | 是否启动数传节点：`true` / `false` |
| `telemetry_device_path` | `/dev/ttyTHS0` | 容器内数传串口路径 |
| `telemetry_baud_rate` | `115200` | 数传串口波特率，须与遥控器配置一致 |

串口波特率允许：9600、19200、38400、57600、115200、230400、460800、921600。设备路径须已通过 `compose.yaml` 映射；传入新路径不会自动修改容器映射。

manager 默认持续发布有效新读数；下位机状态接口预留未订阅，当前没有测厚启停服务。

脚本将 `video_`、`thickness_` 参数映射到对应 XML launch 的参数。视频和设备默认值维护在 XML 中；脚本默认启用数传，直接运行测厚 XML 时其数传默认仍为关闭。流路径、超时等高级设置见 `src/video_streamer_transmission/config/video.yaml`，目前不作为脚本参数。

```bash
# 默认整机启动
bash /workspace/activate/robot.sh

# 同一参数名可用于单模块和整机入口
bash /workspace/activate/single_module/camera.sh video_bitrate_kbps:=8000
bash /workspace/activate/robot.sh video_bitrate_kbps:=8000

# 只采集、打印测厚，不启动数传
bash /workspace/activate/single_module/thickness.sh telemetry_enabled:=false
```

## 退出与重复启动

- **Ctrl+C**：停止本次入口启动的节点。组合入口任一模块退出，会同时停止另一模块。
- 再次运行单独视频入口：先停止本工程旧视频，再使用新参数启动。
- 测厚或组合入口已运行：重复启动会报错，应先在原启动终端按 Ctrl+C，避免争用串口。
- 总入口和组合入口互为同一组功能，选择其中一个运行即可。
- OpenCV 查看窗口属于独立调试程序，按 Q / Esc 关闭，不影响上述模块。

旧 `tools/start_video.sh`、`tools/read_thickness.sh`、`tools/start_video_thickness.sh` 已迁移；新操作统一使用本目录入口。
