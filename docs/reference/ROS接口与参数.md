# ROS 接口与参数参考

文档状态：现行
适用版本：2026-10-21 工作区修改
更新日期：2026-10-21

## 节点启动参数

| 节点 | 参数 | 类型 | 默认值 | 行为 |
|---|---|---|---|---|
| thickness_node | device_path | string | /dev/ttyACM0 | 启动只读，绝对设备路径 |
| thickness_node | baud_rate | int64 | 115200 | 启动只读，支持的 TTY 波特率 |
| telemetry_bridge_node | device_path | string | /dev/ttyTHS0 | 载板串口 1，启动只读 |
| telemetry_bridge_node | baud_rate | int64 | 115200 | 8N1，启动只读 |

TTY 支持 9600、19200、38400、57600、115200、230400、460800、921600，非法路径或不支持的波特率使对应节点启动失败。USB CDC 线路编码波特率不等于 USB 总线速率。

XML launch 额外参数 `telemetry_enabled` 默认 false，控制是否启动数传节点；`telemetry_device_path`、`telemetry_baud_rate` 分别映射数传节点参数。联合脚本默认启动数传。不同节点的 `device_path` 相互独立。测厚统一为 µm，无单位允许参数。

## 话题

| 话题 | 类型 | 发布者 → 订阅者 | QoS |
|---|---|---|---|
| /thickness/reading | ThicknessReading | 采集 → manager | Reliable / Volatile / KeepLast64 |
| /thickness/status | ThicknessStatus | 采集 → 诊断工具 | Reliable / Volatile / KeepLast1 |
| /measurement/thickness | ThicknessReading | manager → 数传 | Reliable / Volatile / KeepLast64 |

### ThicknessReading

`received_at` 是驱动在主机接收/发布时的 ROS 时间，不是仪器硬件采样时间。`sequence` 是驱动进程内计数，重启清零。`device_sn` 是 12 字节 SN，`value` 是未滤波 float32，单位 µm，`unit` 固定为 UNIT_UM，`material_raw` 未解释，`finite` 表示有限浮点。

### ThicknessStatus

包含 `stamp`、`connection_state`（DISCONNECTED / CONNECTING / ONLINE / STALE）、`port_open`、`communication_ready`、设备 SN、最近应答/测量年龄、有效帧/CRC/I/O 错误计数及最多 160 字符的错误文本。年龄使用本地单调时间，从未收到时为 UINT32_MAX。频率 2 Hz。

ONLINE 只表示仪器通信应答新鲜，不表示起降机构已到位或测量精度合格。

### 处理后的测厚数据

manager 默认开启，通过 `/measurement/thickness` 直接发布 `ThicknessReading`，不再增加只含单个字段的包装消息。它只发布标记有效、数值有限且接收时间不早于开启时刻的新读数，不按数值是否变化去重。数传拒绝来源时间在未来或超过 1000 ms 的数据。

### 测厚启停入口

`MeasurementManagerNode::on_measurement_ready(bool)` 是未来下位机状态订阅回调使用的 C++ 入口；当前不创建下位机订阅，也没有测厚启停服务。true 持续开启，false 停止新结果；重复 true 不刷新开启时间。manager 使用单线程 executor。

已移除 MeasurementResult、MeasurementControl、会话/周期字段、`/measurement/control` 和 `/measurement/set_enabled`。取消后不会撤回已发布或排队的数据。安卓 COMMAND_LONG 仍返回 MAV_RESULT_UNSUPPORTED=3。

## 摄像头与视频接口

| 接口 | 类型 | 发布/服务端 | QoS/频率 |
|---|---|---|---|
| `/camera/image_yuv` | sensor_msgs/msg/Image | camera_capture_node | I420；BestEffort / Volatile / KeepLast1；采集目标帧率 |
| `/camera/image_raw` | sensor_msgs/msg/Image | camera_bgr_node | bgr8；BestEffort / Volatile / KeepLast1；有订阅才转换 |
| `/camera/camera_info` | sensor_msgs/msg/CameraInfo | camera_capture_node | 与图像相同时间戳与 QoS |
| `/camera/status` | diagnostic_msgs/msg/DiagnosticArray | camera_capture_node | Reliable / Volatile / KeepLast1；1 Hz |
| `/video/status` | diagnostic_msgs/msg/DiagnosticArray | video_streamer_node | Reliable / Volatile / KeepLast1；1 Hz |
| `/video/set_enabled` | std_srvs/srv/SetBool | video_streamer_node | true 启动 RTSP 服务，false 关闭；不停止摄像头 |

image_yuv 为 encoding=yuv420、step=width、Y/U/V平面共width×height×3/2字节；image_raw 为encoding=bgr8、step=width×3，无填充。header.stamp 为应用取到完整图像的 ROS 时间，不代表硬件曝光时刻。未标定 CameraInfo 的 K[0]=0，不可用于依赖真实内参的测量或定位。

| 启动参数 | 默认值 | 范围/含义 |
|---|---|---|
| source_mode | camera | camera / test，测试源必须显式启用 |
| device_path | /dev/video0 | 容器内 /dev/ 开头设备路径 |
| input_format | MJPEG | MJPEG / YUYV / H264，必须是设备支持模式 |
| width / height | 1920 / 1080 | 偶数；16..4096 / 16..2160，设备须支持 |
| capture_frame_rate | 60 | 1..120，摄像头须支持 |
| frame_rate | 30 | 1..120，编码输出目标 |
| keyframe_interval_frames | 10 | 1..600，编码帧数 |
| bitrate_kbps | 6000 | 100..50000，H.264 目标码率，不是测得网络吞吐 |
| rtsp_port | 8554 | 1024..65535 |

上述常用参数由 XML 同时传给相关组件。高级参数在 video.yaml：frame_id=camera_optical_frame；采集 frame_timeout_ms=2000、reconnect_interval_ms=1000（均至少 100）；视频 stream_path=/live（只允许字母数字、/、_、-）、keyframe_interval_frames=10（1..600）、frame_timeout_ms=2000（至少 100）、max_frame_age_ms=150（至少 1）。所有参数为启动只读参数，改配置后重启。

采集诊断含 state、captured_frames、reconnects、last_error；视频诊断含 state、received_frames、submitted_frames、encoded_frames、dropped_frames、client_count、last_error。client_count 是 RTSP 连接数，不能当作安卓成功解码数；encoded_frames 是 H.264 解析器输出帧数；dropped_frames 计入应用层覆盖/无客户端/过期丢弃，不包含 GStreamer 内部全部丢帧。计数是进程内累计值。

视频状态 STOPPED / LISTENING / STREAMING / WAITING_FRAME / ERROR。LISTENING 仅表示 RTSP 已监听；STREAMING 表示近期有编码输出，不代表无线接收成功。启停服务返回 success 表示本地操作完成，不能作为远端画面验收。

## 2026-10-23 视频正式模块更新

已实现：CameraCaptureNode → `/camera/image_yuv`（紧凑I420）分流；VideoStreamerNode订阅I420直接编码；新增 `src/camera_driver/src/BgrConverterNode.cpp` 独立组件，按需向 `/camera/image_raw` 发布BGR图像。三个组件同进程运行，设备只打开一次。

默认1080p采集60 FPS、编码30 FPS、6000 kbit/s、GOP 10。当前接口与参数以 [视频模块](../modules/视频模块.md) 和 [启动参数](../../activate/README.md) 为准。测试探针仍只在test目录；视觉算法、下位机机构闭环不在此次实现范围。
