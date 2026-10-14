# Docker 与 ROS 2 开发环境

## 环境分工

- 宿主机：Orin 当前 Ubuntu 20.04 / JetPack 5，负责真实硬件驱动、网络、SSH 与 Docker。
- 开发容器：官方 ARM64 ROS 2 Jazzy / Ubuntu 24.04 用户环境，负责编译与运行 ROS 节点。
- 源码：宿主机 `/home/nvidia/robot_ros2`，映射到容器 `/workspace`，不是复制到容器的临时层。
- 容器用户：`robot`，默认 UID/GID 与宿主开发用户一致，避免源码和构建结果变成 root 所有。
- 本次配置只面向 CPU 基础开发，未添加 GPU 运行时、特权模式、USB 摄像头或串口直通。

Docker 不会提供新的 Linux 内核或替换 JetPack 驱动，不能据此认为任意 GPU 库都可跨 JetPack 版本使用。

## 一、宿主机安装

在 Orin 普通用户终端执行：

```bash
sudo bash ~/robot_ros2/tools/install_docker_host.sh
```

脚本安装 Ubuntu 源提供的 Docker Engine、Compose v2、curl、CA 证书；启用 Docker 服务，并将当前 sudo 用户加入 docker 组。Docker 组具有较高宿主机控制权限，仅为受信任开发用户配置。

完成后重新建立 SSH 会话，再验证：

```bash
docker --version
docker compose version
docker info
```

若 Docker 已安装但缺少 Buildx，可以在普通用户终端执行 `python3 tools/install_user_buildx.py`：它从 Ubuntu 软件源下载对应插件包，按 APT 元数据校验 SHA256 后安装用户级插件，不修改系统目录。

不执行系统大版本升级，不更换 JetPack，不配置 sudo 免密。若出现已有 Docker CE 或不同宿主机版本，脚本会停止，避免混装。

## 二、构建并启动

在 Orin 的宿主机终端执行：

```bash
cd ~/robot_ros2
bash tools/dev.sh build
bash tools/dev.sh up
bash tools/dev.sh shell
```

进入容器后，工作目录为 `/workspace`，ROS 环境自动加载。新依赖写入 Dockerfile 或版本化的依赖文件后重新构建，不仅在正在运行的容器中临时安装。

初次构建需要下载官方镜像和软件包；已经构建完成的环境可以在无互联网时启动。下载故障应记录具体端点，不将禁用 TLS 校验或未经确认的第三方镜像作为默认解决办法。

官方基础镜像固定到 ARM64 manifest 摘要，来源记录在 `docker/base-image.lock.json`；更新前应重新验证并记录新摘要。本机 Docker Hub 超时，使用已验证内容摘要与官方一致的 ECR 副本。Python MAVLink 工具使用独立 venv，并保留 ROS 提供的系统 Python 包。

当前网络下载 PyPI 文件较慢，容器构建使用 TUNA 的 HTTPS pip 镜像；`pymavlink`、`fastcrc`、`pyserial` 固定版本。需要改源时在 Dockerfile 构建参数 `PIP_INDEX_URL` 中明确设置，不关闭 TLS 证书检查。

## 三、验证开发环境

在宿主机执行：

```bash
cd ~/robot_ros2
bash tools/dev.sh verify
```

验证脚本会：

1. 检查 Jazzy、rclpy、MAVLink 和串口 Python 库。
2. 在临时目录生成一个最小 C++ ROS 包，真实执行 colcon 编译。
3. 启动 C++ 发布者，由 Python 订阅者接收带随机标记的消息，验证本机 DDS 通信。
4. 编码并解码一帧 MAVLink 2 HEARTBEAT，不访问真实串口。
5. 清理临时测试进程和目录，将结果写入 `log/environment-verification.json`。

结果只证明基础开发环境可用，不代表已实现 C 板通信、测厚仪采集、视频或机器人安全功能。查看最近一次真实执行结果，应以当天开发文档为准。

## 四、VS Code 开发容器

Windows VS Code 安装 Microsoft 的 Dev Containers 扩展后：

1. 通过 Remote - SSH 连接 `orin-nano`。
2. 打开 `/home/nvidia/robot_ros2`。
3. 执行 `Dev Containers: Reopen in Container`。
4. VS Code 按 `.devcontainer/devcontainer.json` 连接开发容器。

后续终端、补全和编译发生在容器内。退出 VS Code 不自动删除开发容器；数据与源码仍在宿主机。

## 五、常用命令

```bash
bash tools/dev.sh status
bash tools/dev.sh logs
bash tools/dev.sh exec bash -lc 'ros2 pkg list'
bash tools/dev.sh down
```

当前默认 ROS_DOMAIN_ID 为 42，DDS 发现范围为 LOCALHOST，使用 Cyclone DDS；后续跨机器通信时再明确调整发现范围和网络配置。MAVLink 串口和 UDP 不会因为使用 DDS 就自动建立。

## 六、开发与产品部署的区别

当前容器的默认命令是 `sleep infinity`，用于开发终端，重启策略是 `no`。Docker 服务可以随主机启动，但这不表示已经配置了机器人业务自启动。

后续真实应用节点创建完成后，再增加独立部署配置、明确的 `ros2 launch` 入口、设备等待与重连、退出行为、日志、健康检测和自动重启策略，并验证上电行为。不要把一个正在运行的空容器标记为机器人已就绪。
