# 模块运行与测试

适用环境：Orin Nano + `robot_ros2_dev` 容器。更新日期：2026-10-20。

本页集中维护运行步骤；参数定义见 [启动说明](../activate/README.md)，代码结构见 [代码目录](architecture/代码目录.md)。

## 1. 进入容器

普通测厚/视频传输测试，在 **Orin 宿主机终端**执行：

```bash
docker start robot_ros2_dev
docker exec -it robot_ros2_dev bash
cd /workspace
```

下面第 2～4 节均在容器内执行。脚本自动加载运行环境，无需手动 source。若需要显示本地图像窗口，直接按第 5 节进入容器。

## 2. 测厚及数传

```bash
bash /workspace/activate/single_module/thickness.sh
```

正常现象：采集、manager、数传三个节点启动；仪器连接状态、序列号和错误计数通过 `ros2 topic echo /thickness/status` 查看。探头产生有效新测量时，终端打印 `seq`、`value` 和 `um`，数传 `written` 计数随发送增加。无新测量时不重发最后一个厚度值；心跳与状态仍可持续发送。

只验证测厚读取：

```bash
bash /workspace/activate/single_module/thickness.sh telemetry_enabled:=false
```

以上两种方式选择一种。按 Ctrl+C 停止。串口应答只证明通信，厚度精度需使用校准片另行验证；完整无线接收需在安卓端核对。

## 3. 摄像头及视频传输

```bash
bash /workspace/activate/single_module/camera.sh
```

正常现象：`camera_capture_node` 与 `video_streamer_node` 加载成功，日志出现设备、分辨率和 `RTSP :8554/live`。客户端未连接时不编码；连接后才开始视频传输。

在宿主机用 `ip -br address show eth0` 确认当前图传网络地址，遥控端使用：

```text
rtsp://<Orin图传网络IP>:8554/live
```

先前地址为 `192.168.144.129`，使用前核对实际 IP。按 Ctrl+C 停止采集与视频。客户端无画面时，见 [视频专项排错](guides/摄像头与视频联调.md)。

## 4. 摄像头和测厚同时运行

```bash
bash /workspace/activate/mix_module/camera_thickness.sh
```

当前总入口 `bash /workspace/activate/robot.sh` 调用相同组合，二选一运行。正常现象为第 2、3 节日志同时出现；测厚和视频独立传输。一次 Ctrl+C 停止两组节点。

## 5. NoMachine 查看原始摄像头图像

### 终端 A：从宿主机进入容器并启动模块

打开 **Nano 的 NoMachine 桌面终端**，执行：

```bash
echo "$DISPLAY"
xhost +SI:localuser:nvidia
docker start robot_ros2_dev
docker exec -it \
  -e DISPLAY="$DISPLAY" \
  -e QT_X11_NO_MITSHM=1 \
  robot_ros2_dev bash
```

`DISPLAY` 应有值，当前桌面通常为 `:1`。进入容器后执行：

```bash
bash /workspace/activate/mix_module/camera_thickness.sh
```

仅看摄像头时，可换成第 3 节的单模块入口。已有采集进程运行时直接进入终端 B，不重复启动。

### 终端 B：显示原始图像

在 **NoMachine 桌面另开一个宿主机终端**，执行：

```bash
docker exec -it \
  -e DISPLAY="$DISPLAY" \
  -e QT_X11_NO_MITSHM=1 \
  robot_ros2_dev bash
```

进入容器后执行：

```bash
source /opt/robot-venv/bin/activate
source /opt/ros/jazzy/setup.bash
source /workspace/install/setup.bash
python3 /workspace/tools/view_raw_image.py
```

正常现象：出现原始图像窗口，显示分辨率和接收 FPS，终端每秒输出统计。该程序订阅 `/camera/image_raw`，与视频使用同一采集源，不再次打开摄像头。

- Q / Esc：关闭查看窗口；S：保存当前帧到 `/workspace/log`。
- 回到终端 A 按 Ctrl+C：停止采集、视频和测厚。
- 容器内 `exit` 返回宿主机，不会关闭 Docker 容器。

### 无窗口或缺少依赖

**宿主机**检查：

```bash
echo "$DISPLAY"
ls -l /tmp/.X11-unix
docker inspect robot_ros2_dev --format '{{range .Mounts}}{{println .Source "->" .Destination}}{{end}}'
```

**容器内**检查：

```bash
echo "$DISPLAY"
ls -l /tmp/.X11-unix
source /opt/robot-venv/bin/activate
python3 -c 'import cv2, cv_bridge; print(cv2.__version__)'
```

容器必须具有 `/tmp/.X11-unix` 挂载，DISPLAY 也须与桌面相同。现有配置在 `compose.yaml`；若容器早于该配置创建，先停止全部模块，再在宿主机执行 `cd /home/nvidia/robot_ros2` 和 `docker compose up -d --force-recreate dev`。重建会中断容器内终端，需要重新进入。

若导入失败，开发镜像应安装 `python3-opencv`、`ros-jazzy-cv-bridge`，虚拟环境应允许系统包；镜像重建步骤见 [环境搭建](guides/开发环境搭建.md)。

## 6. 帧率与延迟的比较

查看器显示的是原始 ROS 图像接收帧率；遥控画面的帧率应从安卓解码/渲染统计中获取。30 FPS 不代表延迟小。

测端到端延迟：摄像头拍摄毫秒计时器，用同一张照片拍下真实计时器和遥控屏幕，计算 `真实时间 − 画面中的时间`，连续采样并记录平均值、最大值。NoMachine 自身会引入显示延迟，因此 OpenCV 窗口与遥控屏幕的差值只能用于相对比较。

Nano 内部分段计时、CV 队列对照和编码测试统一放在 [camera_delay_test](../test/camera_delay_test/README.md)，实测结论见 [测试报告](../test/camera_delay_test/REPORT.md)。

## 2026-10-23 正式视频配置与验证

默认1080p采集60 FPS、编码30 FPS；不支持60 FPS的摄像头使用 `video_capture_frame_rate:=30`。接口分为 `/camera/image_yuv`（I420、编码）和 `/camera/image_raw`（BGR、原图查看），查看器仍订阅后者，不重复打开设备。

```bash
# 宿主机
docker start robot_ros2_dev
docker exec -it robot_ros2_dev bash
# 容器内（单模块 / 组合 / 总入口任选一个）
bash /workspace/activate/single_module/camera.sh
bash /workspace/activate/mix_module/camera_thickness.sh
bash /workspace/activate/robot.sh
# 停止相机和视频
bash /workspace/activate/internal/stop_video.sh
```

原图窗口请在机载实屏或NoMachine桌面按本文已有图形授权流程进入容器，再执行 `python3 /workspace/tools/view_raw_image.py`。NoMachine自身的桌面传输会增加观察延迟，测真值使用实屏同框计时器。

有限回归测试（自动退出并释放自己的视频进程；执行前关闭其他视频）：

```bash
source /opt/ros/jazzy/setup.bash
source /workspace/install/setup.bash
cd /workspace
colcon test --packages-select camera_driver --event-handlers console_direct+
python3 tools/test_video_pipeline.py
python3 tools/test_video_pipeline.py --hardware
```

测试报告写入 `log/video_20261019/`（沿用原测试目录名），本次结果日期和限制记录在当天开发文档。
