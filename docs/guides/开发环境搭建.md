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

## 七、日常开发流程

### 1. 打开项目并进入容器

Windows VS Code 通过 Remote - SSH 连接 `orin-nano`，打开 `/home/nvidia/robot_ros2`，再从命令面板执行 `Dev Containers: Reopen in Container`。Windows 不需要安装 Docker Desktop；实际使用的是 Orin 的 Docker。

在 VS Code 新建终端，检查：

```bash
pwd
whoami
echo "$ROS_DISTRO"
```

预期分别为 `/workspace`、`robot`、`jazzy`。容器中的 `/workspace/src` 与宿主机 `/home/nvidia/robot_ros2/src` 是同一份文件。

若编辑器仍停留在宿主机，可先用命令行开发：

```bash
cd ~/robot_ros2
bash tools/dev.sh up
bash tools/dev.sh shell
```

若提示无法访问 Docker，而新 SSH 终端可以访问，可能是 VS Code Server 保留了旧的用户组。保存工作后执行 `Remote-SSH: Kill VS Code Server on Host...`，再重新连接。

### 2. 先运行现有 ROS 示例

在两个容器终端分别执行以下命令。VS Code 已进入容器时，直接新建第二个终端即可；普通 SSH 终端则各执行一次 `bash tools/dev.sh shell`。

终端一：

```bash
ros2 run demo_nodes_cpp talker
```

终端二：

```bash
ros2 run demo_nodes_py listener
```

发布者连续发送消息，订阅者显示收到的消息。按 Ctrl+C 分别停止。这是现成的 ROS 示例，不是机器人 MAVLink 节点。

### 3. 编译自己的代码

当前 `src` 仅有占位文件。后续创建 ROS 包后，在容器里执行：

```bash
cd /workspace
colcon build --symlink-install
source install/setup.bash
```

随后使用对应包的 `ros2 run` 或 `ros2 launch` 入口启动。`--symlink-install` 有助于 Python 源码和资源的开发，但不会自动重新编译 C++；C++ 修改后仍要执行编译。

| 改动 | 操作 |
| --- | --- |
| ROS 包的 C++ 源码 | colcon 编译，重启节点 |
| Python 源码或配置 | 按包安装方式决定是否需编译，重启节点加载修改 |
| Dockerfile 或容器依赖 | 在宿主机执行 `bash tools/dev.sh build`，然后 `bash tools/dev.sh up`；或在 VS Code 执行 Rebuild Container |
| 只关闭终端后继续开发 | 重新打开终端，不必重新构建镜像 |
| Orin 重新上电 | 宿主机执行 `bash tools/dev.sh up`，再进入容器 |

### 4. 保存、Git 和退出

编译、运行在容器终端执行；当前 Git 提交和推送在 Orin 宿主机终端执行。原因是仓库的 SSH 配置使用宿主机 `/home/nvidia/.ssh/robot_ros2_github`，该私钥未挂入容器。

宿主机示例：

```bash
cd ~/robot_ros2
git status
```

确认本次改动后再按文件选择暂存、提交和推送。不要将私钥复制到镜像中。

`exit` 只退出当前容器 shell；Ctrl+C 停止前台节点。需要停止开发容器时，在宿主机执行 `bash tools/dev.sh down`，宿主机源码仍保留。容器中未写入挂载目录的临时改动可能随容器重建丢失，因此项目源码放在 `/workspace`，环境依赖写进 Dockerfile 或依赖文件。

参考：[VS Code 在远程 Docker 主机开发](https://code.visualstudio.com/remote/advancedcontainers/develop-remote-host)、[Dev Containers 使用说明](https://code.visualstudio.com/docs/devcontainers/containers)。
