# 1080p 图传低延迟 A/B 测试

本次测试全部位于 `test/camera_delay_optimize` 隔离目录，未修改机载端和遥控端正式版本源码。端到端真值仍以同屏计时器照片为准。

## 环境

- 摄像头：MJPEG 1920×1080，可输出 30/60 FPS
- 机载端：Orin Nano，无 NVENC 编码设备，使用 x264 软件编码
- 视频：H.264 baseline，6000 kbit/s，输出 30 FPS，RTSP/RTP/UDP
- 遥控端：MK32，Android 9，Qualcomm msm8953
- 解码器：`OMX.qcom.video.decoder.avc`

## 结果

| 候选 | 采集/输出 FPS | V4L2→H.264 中位数 | 遥控端 RTP→显示中位数 | 结论 |
|---|---:|---:|---:|---|
| 原编码 + 原解码 | 30/30 | 56.8 ms | 约 38 ms | 用户端到端实测约 260 ms |
| 原编码 + Qualcomm 低延迟解码 | 30/30 | 56.8 ms | 17–20 ms | 有效，配置被硬件解码器接受 |
| 4 切片线程 | 30/30 | 30.9 ms | 约 85 ms，约 16 FPS | 否决，msm8953 解码明显变慢 |
| 2 切片线程 | 30/30 | 32.1 ms | 约 25 ms | 有效，整链路净省约 16 ms |
| 60 FPS 采集 + 原编码 | 60/30 | 63.8 ms | 约 17 ms | 编码前等待增加，单独使用收益小 |
| 60 FPS 采集 + 2 切片线程 | 60/30 | 38.0 ms | 约 26 ms | 当前最佳候选，编码稳定 30 FPS |

当前最佳候选相对原测试可从解码环节节省约 18–21 ms，并从采集/编码路径获得约 19 ms 的软件时序改善。60 FPS 采集将摄像头帧周期从 33.3 ms 降至 16.7 ms，但新增的 30 FPS 选帧等待会抵消一部分收益。综合推算端到端约 215–225 ms，需用当前运行画面重新测量。

## 当前候选

```text
camera: 1920x1080 MJPEG @ 60 FPS
video output: 1920x1080 H.264 @ 30 FPS
x264: tune=zerolatency bframes=0 speed-preset=ultrafast threads=2 sliced-threads=true
bitrate: 6000 kbit/s
GOP: 10
client: RTP/UDP + RTCP PLI + Qualcomm low-latency decoder
```

当前候选的机载软件时序：

- 摄像头采集管线：10.8 ms 中位数
- 最新帧选择与复制：8.8 ms 中位数
- 编码与解析：16.2 ms 中位数
- V4L2 输出至 H.264：38.0 ms 中位数，47.7 ms P95
- 机载组件进程约占 2.1 个 CPU 核

## 尚未消除的时间

软件探针从 V4L2 已输出的帧开始计时，无法测量摄像头曝光、摄像头内部 ISP/MJPEG 缓冲、无线链路调度和屏幕扫描。此前本地直显约 100 ms，说明摄像头与本地显示仍占较大比例。若当前实测仍明显高于 225 ms，继续调整 x264 的收益很小，应优先更换低延迟 UVC 摄像头或允许 720p/60 FPS。

## 隔离测试入口

- `live_test/capture60_encode30.launch.xml`：60 FPS 采集、30 FPS 编码
- `live_test/start_live_candidate.sh`：切换 x264 候选
- `live_test/analyze_live.py`：分析当前运行的帧级软件时序

