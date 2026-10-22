#!/usr/bin/env bash
set -Eeuo pipefail
project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "${project_root}"
if [[ ${1:-} == --help || ${1:-} == -h ]]; then
  cat <<'HELP'
用法: bash activate/mix_module/camera_thickness.sh [video_bitrate_kbps:=6000]
      [video_device_path:=/dev/video0] [thickness_device_path:=/dev/ttyACM0]
      [thickness_baud_rate:=115200] [telemetry_enabled:=true|false]
      [telemetry_device_path:=/dev/ttyTHS0] [telemetry_baud_rate:=115200]
视频参数加 video_ 前缀；测厚设备/波特率参数加 thickness_ 前缀。
默认同时启动视频、测厚和数传，manager 默认持续发布有效新读数。
默认视频 6000 kbit/s，测厚 /dev/ttyACM0、115200；无需填写参数即可启动。
先关闭旧视频；旧测厚若在运行则报错，不重复占用串口。Ctrl+C 停止本次两组节点。
HELP
  exit 0
fi
video_arguments=()
thickness_arguments=(telemetry_enabled:=true)
for argument in "$@"; do
  case "${argument}" in
    video_*) video_arguments+=("${argument}") ;;
    thickness_device_path:=*|thickness_baud_rate:=*) thickness_arguments+=("${argument}") ;;
    telemetry_*) thickness_arguments+=("${argument}") ;;
    *) echo "不支持的参数: ${argument}；使用 --help 查看用法。" >&2; exit 2 ;;
  esac
done
# 在宿主机只负责进入容器；两组 launch 在同一容器内受控启动和退出。
if [[ ! -f /.dockerenv && ! -f /run/.containerenv ]]; then
  container_id=$(docker compose ps --status running -q dev)
  if [[ -z ${container_id} ]]; then
    docker compose up -d dev
    container_id=$(docker compose ps --status running -q dev)
  fi
  exec docker exec -it "${container_id}" bash /workspace/activate/mix_module/camera_thickness.sh "$@"
fi
mkdir -p log
exec 7>log/.video-thickness.lock
flock -n 7 || { echo '视频与测厚联合启动脚本已运行，请先在原终端按 Ctrl+C。' >&2; exit 1; }
exec 6>log/.read_thickness.lock
flock -n 6 || { echo '测厚读取已运行，请先在原测厚终端按 Ctrl+C。' >&2; exit 1; }
# 测厚启动脚本自行持锁，先释放本次检查使用的描述符。
flock -u 6
exec 6>&-
video_pid=''
thickness_pid=''
cleanup()
{
  local result=$?
  trap - EXIT INT TERM
  # ROS launch 的子节点可能建立独立进程组，按本次 PID 树逐一清理。
  bash "${project_root}/activate/internal/stop_launch_tree.sh" "${video_pid}" "${thickness_pid}" || true
  wait "${video_pid}" 2>/dev/null || true
  wait "${thickness_pid}" 2>/dev/null || true
  exit "${result}"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
setsid bash activate/single_module/camera.sh "${video_arguments[@]}" 7>&- &
video_pid=$!
setsid bash activate/single_module/thickness.sh "${thickness_arguments[@]}" 7>&- &
thickness_pid=$!
# 任一任务结束则停止另一任务，避免只剩单边进程运行。
wait -n "${video_pid}" "${thickness_pid}"
