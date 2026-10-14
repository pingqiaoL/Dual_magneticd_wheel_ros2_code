# 双磁轮检测机器人 ROS 2 上位机

本仓库用于开发运行在 **NVIDIA Jetson Orin Nano + 达妙载板** 上的机器人上位机软件，主要负责与 STM32 下位机通信、测厚仪数据采集、视频处理和任务管理。

下位机固件独立维护：[Dual_magnetic_wheel-robot](https://github.com/pingqiaoL/Dual_magnetic_wheel-robot)。

## 当前进度

当前提交仅建立开发目录和版本管理，**尚未实现 ROS 节点、MAVLink 收发、测厚仪驱动或视频推流**。

截至 2026-10-14，开发板环境为：

| 项目 | 当前状态 |
| --- | --- |
| 硬件 | Jetson Orin Nano，达妙载板 |
| 系统 | Ubuntu 20.04.6 LTS / ARM64 |
| Jetson Linux | R35.6.1，内核 5.10.216-tegra |
| 开发方式 | Windows VS Code 通过 Remote - SSH 连接 Orin |
| ROS 2 | 尚未安装，部署方案待确定 |
| Docker | 尚未安装 |

目标 ROS 版本建议使用 **ROS 2 Humble**。其官方 Ubuntu 二进制包面向 Ubuntu 22.04：可以在确认达妙载板支持后重刷对应 JetPack 6 系统，也可以保留现有系统并使用匹配架构的容器环境。部署方案确定前，不在 Ubuntu 20.04 中直接添加 Ubuntu 22.04 的软件源。

## 开发目录

Orin 上的工程路径为：

```text
/home/nvidia/robot_ros2
```

该目录同时作为 Git 仓库和后续的 colcon 工作空间：

```text
robot_ros2/
├── src/                # 后续放置 ROS 2 软件包
├── docs/               # 通信约定、接线说明和开发记录
├── tools/              # 后续放置开发与联调工具
├── .gitattributes      # 文本换行规则
├── .gitignore          # 排除编译产物、采集数据和本机配置
└── README.md           # 中文项目说明
```

`src`、`docs`、`tools` 目前仅包含保留目录用的 `.gitkeep` 文件。

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

ROS 安装且实际软件包创建后，再补充经过验证的 `colcon build`、启动和测试命令。当前空工作空间不作为功能验收结果。

## 版本管理约定

- `main` 保存经过检查的代码；较大功能可在独立功能分支开发。
- 不提交 `build/`、`install/`、`log/` 等生成目录。
- 测厚采集数据、rosbag 和录制视频保存在忽略的 `data/`、`recordings/` 等目录中。
- 不提交密码、访问令牌、SSH 私钥或设备本机配置。
- 重刷系统前，将必要修改提交并推送到 GitHub，另外备份未纳入版本管理的数据。

## 参考资料

- [ROS 2 Humble 官方安装说明](https://docs.ros.org/en/humble/Installation/Ubuntu-Install-Debs.html)
- [MAVLink 官方文档](https://mavlink.io/en/)
- [NVIDIA Jetson Linux 文档](https://docs.nvidia.com/jetson/)
