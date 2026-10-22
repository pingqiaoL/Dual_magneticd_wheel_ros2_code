# 图传到达延迟测试

本测试测量 Nano 输出 RTP 视频帧，到遥控器完整接收该帧并通过 UART 返回确认的闭环耗时。**不包含安卓解码和屏幕显示，闭环包含数传回程，不能直接叫作单向图传延迟。** 实测数据见 [VIDEO_LINK_REPORT.md](VIDEO_LINK_REPORT.md)。

## 隔离方式

- 测试脚本与说明只放在 `test/camera_delay_test/`，构建产物和原始数据排除 Git。
- Nano 使用正式包的仪表化副本，在 `log/camera_delay_build_20261020/` 单独编译、使用 ROS 域 79。正式源码和安装目录不覆盖。
- 安卓使用 `app_process` 执行测试 dex，不安装或修改正式 APK；暂时停止正式 App，释放 `/dev/ttyHS0`。
- 视频 TCP/UDP socket 绑定安卓 `eth0` 地址，Wi‑Fi ADB 仅作调试。采样期间日志留在设备上，结束再拉取。
- 测试期间暂停原组合入口，结束后重新启动原命令和原 App。恢复的是新进程，原启动终端会结束；不发送机构动作命令。

## 计时与关联

```text
Nano h264parse 输出 → RTP 打包输出 t0
                           │ 以太网/天空端/遥控链路
                           ↓
                    Android 收齐一帧 a0
                           ↓ 准备 ACK a1
                           │ UART 数传回程
                           ↓
                    Nano 解析 ACK t1
```

Nano 在 `pay0` 输出 pad 记录首包和 marker 末包的单调时间，**这里不是物理网卡出线时刻**。通过 RTP SSRC + timestamp 拼成 64 位帧标识，PTS 再关联 H.264 输出时间。每轮独立 trace 与新 socket 构成会话边界，不把不同轮次数据混合。

Android 根据 RTP 序号连续性、VCL NAL 起始和 marker 位判定完整帧。缺包/乱序保守判为损坏，不补包，不解码。它不是完整 H.264 解码有效性检查；不能代表播放器已经显示正确画面。每秒选择一帧确认，预热 10 秒，再统计 60 秒。

ACK 临时复用 MAVLink 2 PING（消息 4，CRC extra 237），仅供本测试程序解释，不是正式业务协议：

| 字段 | 测试含义 |
|---|---|
| 来源 / 目标 | Android 255/190 → Nano 1/191 |
| `time_usec` | SSRC 高 32 位 + RTP timestamp 低 32 位，按无符号整数匹配 |
| `seq` 最高位 | 1 表示视频 ACK；0 表示普通 PING 响应 |
| `seq` 低 31 位 | ACK 准备间隔 `(a1-a0)`，单位 µs |
| payload | 固定完整 14 B，避免旧解码器对合法尾零截断的不兼容 |

同一 Android 进程、同一串口写锁交错响应校准 PING。Nano 只接受来源、目标、token 和序号全部匹配的响应；2 秒未返回记超时。

- 原始闭环 `L = t1-t0`，无需两端时钟同步。
- `P = a1-a0` 只在安卓本机时钟内相减，不包含串口 write 调用之后的等待。
- 粗估 `L-P-RTT/2`，RTT 取该 ACK 前后 3 秒内 PING 的中位数。依赖数传上下行近似对称、响应长度和处理负载接近；不是精确单向测量。
- 无有效邻近 PING 时不输出粗估。保留负粗估及异常长尾，不能裁剪为零或删除异常后报告。

## 文件与执行

| 文件 | 用途 |
|---|---|
| `extend_rtp_trace.py` | 仅给已生成的测试副本增加 RTP 计时点 |
| `VideoArrivalProbe.java` | Android eth0 UDP 接收、完整帧判断、UART ACK/PING |
| `build_android_probe.sh` | Java 8 字节码与 D8 构建；无需 Gradle、无需重新安装 APK |
| `run_android_probe.sh` | 暂停/恢复 App、执行测试、采样结束拉日志 |
| `video_ack_test.py` | Nano 暂停/恢复入口、启动测试副本、收 ACK 与校准 PING |
| `analyze_video_ack.py` | 精确配对和统计，生成 CSV/JSON |
| `test_video_ack_analysis.py` | 无符号帧 ID、负估计保留、无匹配样本拒绝的边界测试 |
| `smoke_video_ack.py` | 独立域和 8555 端口的合成源配对验证，不算硬件延迟结果 |

已有仪表化构建时，在 Nano **容器终端**执行：

```bash
source /opt/robot-venv/bin/activate
python3 /workspace/test/camera_delay_test/video_ack_test.py \
  --runtime-root /workspace/log/camera_delay_build_20261020 \
  --warmup 10 --seconds 60
```

看到 `READY` 后，40 秒内在 Ubuntu 虚拟机执行（ADB 地址按实际设备调整）：

```bash
cd ~/climb_wheel_software/test/camera_delay_test
bash build_android_probe.sh
~/Android/Sdk/platform-tools/adb -s 192.168.74.178:5555 push \
  build/dex/classes.dex /data/local/tmp/camera_delay_test/classes.dex
bash run_android_probe.sh 192.168.74.178:5555
```

默认接收 75 秒；第二个位置参数可改持续时间，用于短时诊断，正式统计需覆盖预热与采样窗口。测试前应先构建并推送 dex，避免占用 READY 等待时间。准备新副本的完整步骤见本目录 `run_test.sh`；运行 `extend_rtp_trace.py` 后必须重新编译测试副本，不能只修改未重建的文件。

结果分析与恢复校验（容器内）：

```bash
python3 /workspace/test/camera_delay_test/analyze_video_ack.py \
  /workspace/test/camera_delay_test/results/<本轮 video_ack 目录>
python3 /workspace/test/camera_delay_test/verify_runtime.py \
  --runtime-root /workspace/log/camera_delay_build_20261020 --expected-viewers 0
```

本轮用户已关闭原 CV 查看器，因此恢复校验预期为 0 个窗口进程。`results/` 保存逐帧 trace、UART 记录、摘要和恢复日志；Android 日志在虚拟机同目录的 `results/android_时间戳/`。

## 串口配置诊断

本机 MK32 的 `stty` 逐项负标志配置实测未得到预期状态，出现 `istrip` 开启，导致 `FD` 变成 `7D`，前两轮无法识别校准 PING。`raw` 组合也未完全清除 `parmrk`。测试程序现读取 `stty -g`，保留波特率和控制字符，用保存格式设置原始输入/输出/本地标志为 0、控制位为 8N1，无硬件流控，随后读回严格核验。核验失败直接退出，不继续给出校准结果。这是测试配置修复，未修改正式 App 的串口实现。
