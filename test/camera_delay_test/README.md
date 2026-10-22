# Nano 摄像头分段延迟测试

本目录集中保存测试代码、操作文档和结果说明。正式业务源码不增加测试依赖。生成的仪表化副本在 `/workspace/log/camera_delay_build_*` 独立构建；原始 CSV 在本目录 `results/`，已排除 Git。

## 测试依据与边界

用户提供的直连屏幕照片：原计时器 19.89 s，CV 画面 19.75 s，遥控画面约 19.52 s；单张照片估计延迟分别为 140 ms 和 370 ms。约 230 ms 的差值用于提出排查方向，不能直接等同于网络耗时。

代码计时从 V4L2 元素输出的 MJPEG 帧开始，覆盖解码、BGR 拷贝、ROS 发布与接收、CV 显示调用、等待编码和 H.264 输出。它不覆盖传感器曝光、摄像头内部缓存和完整 USB 驱动等待，也不代表物理屏幕刷新或安卓端到端延迟。

## 文件用途

| 文件 | 作用 |
|---|---|
| `run_test.sh` | 生成副本、独立编译、运行完整矩阵 |
| `prepare_workspace.py` | 复制当前两个包，校验源码片段并仅修改测试副本；保留正式源码 SHA256 清单 |
| `DelayTrace.hpp` | CLOCK_MONOTONIC 计时，短 CSV 行追加写入，每次记录一次 write |
| `run_matrix.py` | 停止已识别的组合入口和原始查看器，运行对照，退出后重新启动原入口 |
| `measure_viewer.py` | 无窗口 / 原回调显示方式 / 最新帧方式的统计订阅器 |
| `analyze.py` | 匹配同一帧，输出逐帧明细、汇总 JSON 和 Markdown |
| `stop_test.py` | 中断本测试 runner，由其 finally 执行清理与恢复 |
| `verify_runtime.py` | 核对正式源码校验和、恢复后的组件库及查看器进程 |
| `test_analysis.py` | 验证预热排除、帧配对和负耗时拒绝 |
| `telemetry_rtt.py` | 独占数传串口测量带关联标记的 PING 往返时间 |
| `TELEMETRY_HANDSHAKE.md` | 数传实测、视频到达确认的字段及计算边界 |
| `video_link_precheck.py` | 宿主机检查 RTSP OPTIONS、现有连接、ICMP 与 ARP，不计作图传延迟 |
| `VIDEO_LINK_TEST.md` | 真实图传接收 ACK 测试的隔离方式、字段、脚本与两端运行步骤 |
| `VIDEO_LINK_REPORT.md` | 视频到达闭环、同轮数传 RTT、校准误差和恢复校验结果 |
| `REPORT.md` | 本轮实测结论与验证边界 |

## 执行方法

在 Orin 宿主机进入容器；DISPLAY 应指向需要测试的 Nano 桌面：

```bash
docker exec -it -e DISPLAY="$DISPLAY" -e QT_X11_NO_MITSHM=1 robot_ros2_dev bash
```

容器内执行：

```bash
bash /workspace/test/camera_delay_test/run_test.sh
```

脚本结束后自动汇总。需要重新分析时，将终端输出 `RESULTS=` 后的目录填入以下命令：

```bash
python3 /workspace/test/camera_delay_test/analyze.py \
  /workspace/test/camera_delay_test/results/<本轮时间戳>
```

测试会暂时中断当前视频和测厚，不发送机构动作命令。仅自动识别现行组合启动脚本和 `tools/view_raw_image.py`，多个会话或未识别节点会中止测试。自动恢复是重新启动入口，原终端会结束，恢复后的输出保存在结果目录 `restored_*.log`。图像查看窗口标题含测试模式，按 Q 会提前结束测试，该轮不能作为完整数据。

需提前结束时，在另一容器终端执行：

```bash
python3 /workspace/test/camera_delay_test/stop_test.py
```

不能用 kill -9 中断 runner，否则 finally 无法执行。

## 五组对照

每组预热 10 秒、统计 60 秒。相同设备 `/dev/video0`、MJPEG、1280×720、30 FPS；编码组为 x264 veryfast、zerolatency、6000 kbit/s、GOP 30、B 帧 0，与当前正式配置一致。编译优化级别与正式构建保持一致。

| 组 | 运行内容 | 目的 |
|---|---|---|
| A_headless | 摄像头 + 深度 1 的无窗口订阅器 | 采集和进程间 ROS 传递基线 |
| B_legacy_cv | 摄像头 + 深度 5、回调内转换与显示 | 重现原查看器的主要调度方式 |
| C_latest_cv | 摄像头 + 深度 1、后台接收、主线程显示最新帧 | 判断显示阻塞和旧帧队列影响 |
| D_video | 摄像头 + 视频 + 无窗口订阅器 | 视频处理路径与 CPU 负载 |
| E_full | 摄像头 + 视频 + 最新帧窗口 + 测厚数传 | 当前整机负载下的分段耗时 |

B 是带计时点的等效查看器，叠字和日志细节与正式查看器略有差异；B/C 同时改变队列和线程方式，结果用于评价组合方案，不能单独量化某一个改动。C/E 使用接收线程，受 Python GIL 影响，必须结合实测评估。

D/E 由回环 RTSP/TCP 接收器拉流至 fakesink，确保编码持续工作。没有将解码器或屏幕呈现负载计入该接收器；遥控器如保持连接，可能增加服务端客户端数量，需结合日志解释。这两组不等于无线链路验收。

测试使用 ROS_DOMAIN_ID=79，避免与原域 42 的其他订阅者混用。单独域测试只用于受控对照，正式系统域上的消息年龄应另行核对。

## 计时点与计算

所有阶段使用 Linux CLOCK_MONOTONIC。ROS header 时间只作为帧关联 ID，不拿 ROS 系统时间与单调时间相减。原始数据行：`monotonic_ns,stage,frame_id,related_id`。

| 结果字段 | 范围 |
|---|---|
| `capture_pipeline_ms` | V4L2 src pad → appsink 拉取样本，含解码和该段等待 |
| `copy_and_status_ms` | 拉取样本 → 像素拷贝完成及设备状态更新 |
| `publish_prepare_ms` | 拷贝后 → 发布调用前 |
| `ros_to_viewer_ms` | 发布调用前 → Python 订阅回调开始 |
| `viewer_wait_ms` | 回调开始 → 显示循环开始处理该帧 |
| `cv_convert_ms` | cv_bridge 图像转换调用 |
| `cv_overlay_ms` | 叠字调用 |
| `cv_gui_call_ms` | imshow + waitKey 调用时间，非实际呈现时间 |
| `source_to_gui_return_ms` | V4L2 src pad → GUI 调用返回，非玻璃到玻璃延迟 |
| `ros_to_video_ms` | 发布调用前 → 视频回调通过格式/年龄检查 |
| `video_wait_and_copy_ms` | 视频回调 → appsrc push 前，含线程等待和数据拷贝 |
| `encoder_prequeue_ms` | appsrc push 前 → x264 输入 pad，含队列和格式转换 |
| `encode_and_parse_ms` | x264 输入 pad → h264parse 输出 pad |
| `video_callback_to_encoded_ms` | 视频回调 → 对应 H.264 输出 |
| `source_to_encoded_ms` | V4L2 src pad → 对应 H.264 输出 |

相机 PTS 关联 source/sample/copy；ROS header 时间 ID 关联 publish/receive/video；appsrc PTS 关联编码入口和输出。x264 输出 PTS 可能存在固定偏移，分析器根据首个输入/输出求偏移，再验证全部输出的精确 ID 匹配率至少 95%，不足则拒绝给出编码耗时。当前无 B 帧且每组单一媒体生命周期；其他编码模式需重新设计配对。

## 输出和判断

- 每组 `frames.csv`：同一帧的各段单调时间；空值表示该分支没有处理此帧。
- 每组 `summary.json`：样本数、平均值、中位数、P95、最大值、各阶段帧率及编码配对率。
- `resources.csv`：每秒 CPU tick、可用内存、温度和频率；CPU 百分比按所有核心总能力归一化。
- 总 `SUMMARY.md`：组间比较。预热阶段不计入统计；中断或失败矩阵不输出完整测试结论。

日志和计时写入本身有开销，本轮没有进行完全无计时点的等负载量化。不同组中位数不能直接相减解释为精确环节耗时。

实际显示仍用同帧计时器照片测量：真实计时器、Nano 直连显示器、NoMachine 画面和遥控器同时入镜。至少采集 20 张或使用高帧率录像；软件 GUI 返回和编码输出计时不替代这项测量。
