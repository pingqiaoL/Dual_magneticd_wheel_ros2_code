# 磁轮检测机器人上位机

Orin Nano 上的 ROS 2 Jazzy 工程。目前支持测厚仪采集、MAVLink 数传、摄像头采集及 H.264/RTSP 视频传输。测厚默认开启，收到有效新测量后上传一次，单位统一为 µm。

## 1. 进入运行环境

| 环境 | 终端示例 | 工程目录 |
|---|---|---|
| Orin 宿主机（SSH / NoMachine） | `nvidia@ubuntu` | `/home/nvidia/robot_ros2` |
| ROS 2 Docker 容器 | `robot@ubuntu` | `/workspace` |

在 **Orin 宿主机终端**执行：

```bash
docker ps -a
docker start robot_ros2_dev
docker exec -it robot_ros2_dev bash
cd /workspace
```

容器内已有 ROS 2 和项目依赖。宿主机上找不到 `/opt/ros/jazzy`、`/opt/robot-venv` 或 `/workspace` 时，先进入容器。首次创建开发环境见 [环境搭建](docs/guides/开发环境搭建.md)。

## 2. 启动当前整机功能

在 **容器终端**执行：

```bash
bash /workspace/activate/robot.sh
```

该入口同时启动摄像头、视频传输、测厚采集、测厚管理和数传。默认使用：

| 功能 | 默认配置 |
|---|---|
| 摄像头 | `/dev/video0`，MJPEG，1920 × 1080，采集60 FPS |
| 视频 | H.264，输出30 FPS，6000 kbit/s，GOP 10，RTSP `:8554/live` |
| 测厚仪 | `/dev/ttyACM0`，115200 |
| 数传 | `/dev/ttyTHS0`，115200，默认启用 |

启动后应看到摄像头组件加载、RTSP 监听及测厚/数传节点启动日志。有有效新读数时打印测量值；仪器状态通过 `/thickness/status` 查看。遥控端连接 RTSP 后才开始视频编码。

在启动终端按 **Ctrl+C** 停止本次节点，容器保持运行。脚本使用已有构建结果；修改 C++ 或消息后需要重新编译。

## 3. 按任务查阅

- [模块测试](docs/module_test.md)：单模块、组合运行、NoMachine 查看原始图像及检查方法。
- [启动说明](activate/README.md)：所有启动入口、参数及停止行为。
- [代码目录](docs/architecture/代码目录.md)：软件框架树。
- [系统架构](docs/architecture/系统架构.md)：分层和部署职责。
- [文档导航](docs/README.md)：接口、协议、专项排错和开发记录。

视频质量和延迟须在实际链路验收；RTSP 监听或编码状态本身不能证明遥控端已正常显示。
