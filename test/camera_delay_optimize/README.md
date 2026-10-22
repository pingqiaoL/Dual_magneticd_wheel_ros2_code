# 1080p 图传优化测试

本目录只保存测试代码和结果，不修改正式摄像头或视频模块。测试目标是确认 `/dev/video0` 在 MJPEG 1920×1080@30 FPS 下的实际采集、编码帧率和 Nano 内部分段延迟，并比较 H.264 6000 与 8000 kbit/s。

## 运行

在 `robot_ros2_dev` 容器内执行：

```bash
bash /workspace/test/camera_delay_optimize/run_test.sh
```

脚本会暂时停止当前整机入口，分别运行两组 35 秒测试，结束或正常异常退出时恢复原入口。测试副本构建在 `/workspace/log/camera_delay_optimize_*`，正式源码不加入计时点。

## 输出

结果位于 `results/run_<时间>/`：

- `REPORT.md`：两组配置的帧率、分段延迟、CPU 和网络统计。
- `summary.json`：完整机器可读结果。
- `<配置>/frames.csv`：同帧分段计时。
- `<配置>/launch.log`：摄像头和编码器日志。
- `<配置>/resources.csv`：CPU、内存、网口及温度采样。

`V4L2→H.264` 从驱动交给 GStreamer 的压缩帧开始，不含曝光、摄像头内部缓存、无线传输、安卓解码和屏幕刷新。完整端到端延迟仍需同屏计时器或高帧率录像。

## 首轮结论

2026-10-22 实机测试表明，摄像头可以稳定输出 MJPEG 1920×1080@30 FPS，但当前 Orin Nano 软件 H.264 管线无法维持 30 FPS：6000 kbit/s 输出约 23.13 FPS，8000 kbit/s 输出约 21.23 FPS。对应的 V4L2 输出到 H.264 输出中位数分别为 90.14 ms 和 95.63 ms。因此暂不将 1080p 作为正式图传默认值。详细结果见 [REPORT.md](REPORT.md)。
