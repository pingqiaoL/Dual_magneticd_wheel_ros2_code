# 摄像头与图传延迟排查

2026-10-22 的数传 RTT、Nano 编码分段、真实图传 ACK 闭环和正式 App 稳态播放复测见 [数传与图传延迟复测](RETEST_2026-10-22.md)，机器摘要为 `RETEST_2026-10-22.json`。

当前已完成优化，先读 [优化与验收 OPTIMIZATION.md](OPTIMIZATION.md)，机器摘要为 `OPTIMIZATION_SUMMARY.json`。优化前的 [排查结果 REPORT.md](REPORT.md) 和 `SUMMARY.json` 保留为历史证据。测试与副本集中在本目录，通过实测选定的改动已应用到正式源码和安卓 0.8.10。

## 本轮范围

重新采集正式 App 的稳定播放与三次启动数据，读取 Nano 参数、资源、帧计数，确认以太网 UDP 传输。此前同日的 Nano 分段计时、视频 ACK 结果复制到本目录 `results/prior_*` 作为补充证据，不伪装为本轮重新采集。

原始结果在 `results/`，排除 Git。Nano 与 Ubuntu 虚拟机均有 `test/camera_delay/`，最终汇总集中到 Nano；不覆盖原 `camera_delay_test/`。

## 脚本

| 文件 | 用途 |
|---|---|
| `collect_nano.py` | 读取正式 ROS 参数/状态、CPU、内存、网卡计数；不重启节点 |
| `collect_android.py` | 读取正式 App 已有诊断日志，按采样前后窗口取增量；可重新打开 App |
| `inspect_transport.py` | 短时被动读取以太网包头，确认发送方向 UDP；不保存视频载荷 |
| `analyze_overall.py` | 分析各端自身时钟内的数据，不跨设备直接相减 |
| `test_analysis.py` | 防止旧启动周期的 uptime 混入当前帧率，以及无新数据时误用旧日志 |

## Nano 执行

宿主机进入容器：

```bash
docker exec -it robot_ros2_dev bash
source /opt/ros/jazzy/setup.bash
source /workspace/install/setup.bash
export ROS_DOMAIN_ID=42
python3 /workspace/test/camera_delay/collect_nano.py --seconds 75
```

使用终端输出的 `RESULTS=` 目录分析：

```bash
python3 /workspace/test/camera_delay/analyze_overall.py nano \
  /workspace/test/camera_delay/results/<nano_时间戳>
```

短时确认网络传输（宿主机执行，使用现有容器 root 的原始套接字能力）：

```bash
docker exec --user 0 robot_ros2_dev python3 \
  /workspace/test/camera_delay/inspect_transport.py
```

此命令不需要重新配置网络。没有原始套接字权限时停止，不修改系统权限或网络参数。

## Ubuntu 虚拟机执行

保持遥控器屏幕亮起、App 在前台并播放真实视频，USB 数据线拔出，以太网可用；Wi‑Fi ADB 地址按实际设备修改：

```bash
cd ~/climb_wheel_software/test/camera_delay
python3 collect_android.py --device 192.168.74.178:5555 --seconds 75
python3 analyze_overall.py android results/<android_时间戳>
```

需要测重新连接首帧时增加 `--restart`；它会 force-stop 并重新打开 App，短暂中断视频和 App 的数传处理，但不安装 APK、不清日志。单纯稳定播放测试不加这个参数。

```bash
python3 collect_android.py --device 192.168.74.178:5555 --restart --seconds 75
```

## 解释规则

- 只分析采样开始后新增的日志；uptime 会在重启后重复，不能仅按 uptime 筛选历史文件。
- 长轮次前 15 秒从稳定播放统计中排除；20 秒短轮次主要用于首帧观察，不能代替长期稳定性验证。
- `buffered_ms` 是待播媒体时长；`release_lead_us` 是回调时距离计划释放的时间，两者有潜在重叠，不能当作两个独立环节相加。
- 渲染帧率来自有效时间段内计数增量，不使用开机累计计数/测试时长。
- 网卡零错误不代表无线零丢包；应用渲染丢帧也不是 RTP 丢包计数。
- 首帧等待、接收闭环和实物到屏幕延迟是不同指标，具体边界见报告。

## 优化测试

`prepare_android_variant.py` 与 `prepare_nano_variant.py` 生成隔离副本；`run_nano_variant.py` 管理发送候选并恢复原入口，可选择两次受控暂停。`promote_android.py`、`promote_nano.py` 是本轮一次性应用脚本，包含并发修改检查，不作为日常启动入口。`verify_optimized_runtime.py` 校验当前正式源码与组件库。独立集成检查为 `test_latest_pipeline.py`。

`optimization_workspaces/` 和 `results/` 排除 Git；`COLCON_IGNORE` 防止根工作区扫描测试副本。原始数值与失败轮次均保留，详见优化报告。
