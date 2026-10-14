# 双磁轮检测机器人 ROS 2 上位机

本仓库用于开发运行在 **NVIDIA Jetson Orin Nano + 达妙载板** 上的机器人上位机软件，主要负责与 STM32 下位机通信、测厚仪数据采集、视频处理和任务管理。

下位机固件独立维护：[Dual_magnetic_wheel-robot](https://github.com/pingqiaoL/Dual_magnetic_wheel-robot)。

## 当前进度

已建立开发目录和版本管理，完成 **Docker + ROS 2 Jazzy** 开发环境安装，并通过 C++ 编译、C++ 到 Python 的 DDS 消息通信和 MAVLink 2 编解码验证。**机器人 ROS 节点、实机 MAVLink 收发、测厚仪驱动和视频推流尚未实现**。实际过程与验证结果记录在当天开发文档中。

截至 2026-10-14，开发板环境为：

| 项目 | 当前状态 |
| --- | --- |
| 硬件 | Jetson Orin Nano，达妙载板 |
| 系统 | Ubuntu 20.04.6 LTS / ARM64 |
| Jetson Linux | R35.6.1，内核 5.10.216-tegra |
| 开发方式 | Windows VS Code 通过 Remote - SSH 连接 Orin |
| ROS 2 | Jazzy / Ubuntu 24.04 ARM64 容器，基础环境验证通过 |
| Docker | Engine 26.1.3，Compose 2.27.1，Buildx 0.14.1 |

当前保留宿主机 Ubuntu 20.04 与 JetPack，在容器中运行 ROS 2 Jazzy。选择 Jazzy 是考虑新项目的维护周期：Humble 社区支持至 2027 年 5 月，Jazzy 至 2029 年 5 月。宿主机不添加 Ubuntu 24.04 的软件源，不因容器内使用新用户环境就替换板级驱动。

- [Docker 与 ROS 2 开发环境](docs/Docker与ROS2开发环境.md)
- [2026-10-14 开发文档](docs/2026-10-14开发文档.md)

当前环境为 CPU 基础开发环境。GPU、视频和实际外设接入后需要另行验证与 JetPack 的兼容性。

## 开发目录

Orin 上的工程路径为：

```text
/home/nvidia/robot_ros2
```

该目录同时作为 Git 仓库和后续的 colcon 工作空间：

```text
robot_ros2/
├── .devcontainer/      # VS Code 开发容器入口
├── docker/             # 容器镜像、依赖与环境脚本
├── src/                # 后续放置机器人 ROS 2 软件包
├── docs/               # 通信约定、接线说明和开发记录
├── tools/              # 后续放置开发与联调工具
├── .gitattributes      # 文本换行规则
├── .gitignore          # 排除编译产物、采集数据和本机配置
├── compose.yaml        # 开发容器配置
├── AGENTS.md           # 开发与每日文档约定
└── README.md           # 中文项目说明
```

`src` 暂无机器人 ROS 包；`docs` 与 `tools` 已增加环境说明、每日开发记录和环境配置工具。

## 软件职责

| 层次 | 计划职责 |
| --- | --- |
| ROS 2 上位机 | MAVLink 适配、测厚采集、测量任务、状态汇总、数据记录和视频处理 |
| STM32 / NuttX 下位机 | 遥控解析、解锁互锁、失效保护、控制分配、电机与执行机构控制 |
| MK32 地面端 | 显示视频和测厚结果、发送操作请求 |

计划中的数据路径：

```text
MK32 遥控 → 天空端 SBUS → 下位机 → 电机与执行机构
Orin ROS 2 ↔ UART / MAVLink ↔ 下位机
USB 测厚仪 → Orin → 天空端数传 → MK32 界面
USB 摄像头 → Orin → 网络视频 → 天空端 LAN → MK32
```

以上为目标架构，不代表通信协议、驱动和接口已经实现。串口设备名、引脚、电平与波特率需要实机核对，不根据接口编号直接推定。

## 第一阶段：MAVLink 基础通信

先开发 `cboard_mavlink_bridge`，建立上位机与下位机之间可观测、可恢复的通信链路：

1. 确认 ROS 环境和实际串口，核对接线及端口占用。
2. 两端接入同一消息定义版本的 MAVLink 库。
3. 完成 `HEARTBEAT` 收发和目标设备识别。
4. 完成链路超时、断线重连及收发计数。
5. 使用 `PING` 验证双向收发，再增加状态遥测与命令应答。

现有下位机固件仍需增加 MAVLink 通信模块，仅创建上位机仓库不能直接完成硬件通信。初期优先使用标准消息；自定义消息增加前，应记录消息 ID、字段类型、单位、频率和协议版本。

心跳表示通信组件在线，不直接等于机器人初始化完成或允许解锁。上位机通信不绕过下位机的 SA 互锁和公共输出安全检查。

后续按需要增加的 ROS 包：

- `robot_interfaces`：自定义消息、服务与动作接口。
- `robot_bringup`：启动文件与参数配置。
- 测厚仪采集、测量任务管理、状态汇总及数据记录等功能包。

上述软件包目前尚未创建。

## 日常开发

连接 Orin 后，在 VS Code 中打开 `/home/nvidia/robot_ros2`。文件编辑、终端命令、后续编译和运行均发生在 Orin 上。

查看修改：

```bash
cd ~/robot_ros2
git status
git diff
```

提交并推送：

```bash
git add README.md src docs tools .gitignore .gitattributes
git commit -m "说明本次修改内容"
git push origin main
```

在工作区干净时获取远端更新：

```bash
git pull --ff-only origin main
```

进入开发容器与验证的操作见环境文档。环境验证使用临时测试包完成真实编译和消息收发，不会把测试节点作为机器人业务包留在 `src` 中；当前空业务工作空间不作为机器人功能验收结果。

## 版本管理约定

- `main` 保存经过检查的代码；较大功能可在独立功能分支开发。
- 不提交 `build/`、`install/`、`log/` 等生成目录。
- 测厚采集数据、rosbag 和录制视频保存在忽略的 `data/`、`recordings/` 等目录中。
- 不提交密码、访问令牌、SSH 私钥或设备本机配置。
- 重刷系统前，将必要修改提交并推送到 GitHub，另外备份未纳入版本管理的数据。

## 参考资料

- [ROS 2 Jazzy 官方文档](https://docs.ros.org/en/jazzy/)
- [MAVLink 官方文档](https://mavlink.io/en/)
- [NVIDIA Jetson Linux 文档](https://docs.nvidia.com/jetson/)
