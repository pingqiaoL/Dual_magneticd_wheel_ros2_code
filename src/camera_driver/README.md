# camera_driver

CameraDevice 唯一打开设备，解码为紧凑 I420；CameraCaptureNode 发布 `/camera/image_yuv`、相机信息与状态。BgrConverterNode 在有订阅者时独立转换并发布 `/camera/image_raw`（bgr8），供原图查看和算法使用。

默认 MJPEG 1920×1080@60，编码分支独立输出30 FPS。合成源布局、停止/重启及真实设备回环由本次测试记录区分。

正式启动使用 `activate/single_module/camera.sh`，将三个组件加载在同一多线程容器。单独启动采集节点只发布 I420，如需 BGR 同时启动 camera_bgr_node。

布局、模块职责及参数见 [视频模块](../../docs/modules/视频模块.md)，操作见 [模块测试](../../docs/module_test.md)。CameraInfo 未标定，不用于精度验收。
