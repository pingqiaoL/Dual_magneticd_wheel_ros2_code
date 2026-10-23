# 机载上位机当前代码图

核对日期：2026-10-23。上位机指 Orin Nano，安卓属于遥控端。

- `orin_onboard_current.svg`：矢量图，适合放大、排版与后续编辑。
- `orin_onboard_current.png`：图片预览。

依据容器 `/workspace/src` 的节点、驱动、协议和 launch，以及 `/workspace/activate/README.md` 绘制。仅展示当前代码，不调整模块划分，不修改业务源码。

关键边界：

- `MeasurementManagerNode` 当前默认开启；下位机状态回调预留但未订阅。
- 数传 `TelemetryDriver` 内部线程用 poll/eventfd 等待，应用不周期调用 process_io。
- 摄像头发布 I420；图传直接编码，BGR 在独立组件中按需转换。
- 默认启动为 1920×1080、60 FPS 采集、30 FPS 编码、6000 kbps、GOP 10；这些是配置，不代表本次重新实测性能。
- 正式视频编码仍为 threads=2、sliced-threads=true。FPV 单线程兼容服务及请求代理属于独立 test，未混入正式架构。
- STM32 XRCE-DDS、机构就绪状态和视觉算法尚未实现接入。

图中驱动与协议在同一包内使用，不是独立 ROS 节点；只有标为 Node 的类对应节点或组件。测厚仪和摄像头是外部输入，MK32 天空端和遥控播放器是外部输出。
