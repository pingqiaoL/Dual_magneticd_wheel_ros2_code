#!/usr/bin/env bash
# 在 Orin 本机终端运行：sudo bash ~/robot_ros2/tools/install_docker_host.sh
# 仅安装开发所需的软件，不升级 JetPack、不修改设备树、不配置 sudo 免密。
set -Eeuo pipefail

if [[ ${EUID} -ne 0 ]]; then
  echo '请在 Orin 终端使用 sudo 执行本脚本，并在本机输入密码。' >&2
  exit 1
fi

developer=${SUDO_USER:-}
if [[ -z ${developer} || ${developer} == root ]]; then
  echo '请从普通开发用户会话使用 sudo 执行，以便正确配置 Docker 用户组。' >&2
  exit 1
fi

if [[ $(dpkg --print-architecture) != arm64 ]]; then
  echo '本脚本面向 ARM64 Orin，已停止。' >&2
  exit 1
fi

source /etc/os-release
if [[ ${ID} != ubuntu || ${VERSION_ID} != 20.04 ]]; then
  echo '本次脚本仅在当前 Ubuntu 20.04 环境使用；更换系统后请重新核对安装方案。' >&2
  exit 1
fi

if dpkg-query -W -f='${Status}' docker-ce 2>/dev/null | grep -q 'install ok installed'; then
  echo '检测到已有 Docker CE；为避免混装，已停止，请先检查现有安装。' >&2
  exit 1
fi

apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  docker.io docker-compose-v2 docker-buildx ca-certificates curl

systemctl enable --now docker.service
usermod -aG docker "${developer}"

echo 'Docker 安装完成：'
docker --version
docker compose version
systemctl is-active docker.service
echo '开发用户已加入 docker 组；请重新建立 SSH 会话，使新组权限生效。'
echo '本脚本未安装 ROS 到宿主机，ROS 将在项目容器内构建。'
