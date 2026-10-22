#!/usr/bin/env bash
set -Eeuo pipefail
project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "${project_root}"
if [[ ${1:-} == --help || ${1:-} == -h ]]; then
  echo '用法: bash activate/single_module/camera.sh [video_source_mode:=camera|test] [video_device_path:=/dev/video0]'
  echo '      [video_input_format:=MJPEG|YUYV|H264] [video_width:=1280] [video_height:=720]'
  echo '      [video_frame_rate:=30] [video_bitrate_kbps:=6000] [video_rtsp_port:=8554]'
  echo '宿主机/容器自适应。默认实际摄像头；测试图像必须显式指定 video_source_mode:=test。'
  echo '再次运行会先停止本工程旧的视频 launch，再按本次参数重新启动。'
  exit 0
fi
launch_arguments=()
for argument in "$@"; do
  [[ ${argument} == video_* ]] || { echo "视频参数必须以 video_ 开头: ${argument}" >&2; exit 2; }
  launch_arguments+=("${argument#video_}")
done
set -- "${launch_arguments[@]}"
for argument in "$@"; do
  case "${argument}" in
    source_mode:=camera|source_mode:=test|device_path:=/dev/*|input_format:=MJPEG|input_format:=YUYV|input_format:=H264) ;;
    width:=[0-9]*|height:=[0-9]*|frame_rate:=[0-9]*|bitrate_kbps:=[0-9]*|rtsp_port:=[0-9]*) ;;
    *) echo "不支持的参数: ${argument}" >&2; exit 2 ;;
  esac
done
mkdir -p log
# 串行处理重启，避免两个终端同时停止和启动视频。
exec 8>log/.video-restart.lock
flock -w 30 8 || { echo '另一个终端正在重启视频。' >&2; exit 1; }
bash "${project_root}/activate/internal/stop_video.sh"
exec 9>log/.video.lock
flock -w 20 9 || { echo '旧视频启动脚本尚未释放锁，请检查原终端。' >&2; exit 1; }
flock -u 8
exec 8>&-
if [[ -f /.dockerenv || -f /run/.containerenv ]]; then
  video_launch_pid=''
  cleanup_container()
  {
    local result=$?
    trap - EXIT INT TERM
    if [[ -n ${video_launch_pid} ]]; then
      bash "${project_root}/activate/internal/stop_launch_tree.sh" "${video_launch_pid}" || true
      wait "${video_launch_pid}" 2>/dev/null || true
    fi
    exit "${result}"
  }
  trap cleanup_container EXIT
  trap 'exit 130' INT
  trap 'exit 143' TERM
  setsid /usr/local/bin/robot-entrypoint ros2 launch video_streamer_transmission video.launch.xml "$@" &
  video_launch_pid=$!
  wait "${video_launch_pid}"
  exit 0
fi
docker info >/dev/null
container_id=$(docker compose ps --status running -q dev)
if [[ -z ${container_id} ]]; then
  docker compose up -d dev
  container_id=$(docker compose ps --status running -q dev)
fi
[[ -n ${container_id} ]] || { echo '开发容器没有运行。' >&2; exit 1; }
run_key="$$-${RANDOM}"
run_directory="${project_root}/log/.video-session-${run_key}"
container_directory="/workspace/log/.video-session-${run_key}"
mkdir "${run_directory}"
client_pid=''
cleanup()
{
  local result=$?
  trap - EXIT INT TERM
  : > "${run_directory}/cancel"
  docker exec "${container_id}" bash -c '
    if [[ -r "$1/pid" ]]; then
      read -r launch_pid < "$1/pid"
      if [[ ${launch_pid} =~ ^[0-9]+$ && -r /proc/${launch_pid}/environ ]] &&
         tr "\0" "\n" < "/proc/${launch_pid}/environ" | grep -Fxq "ROBOT_VIDEO_RUN=$2"; then
        kill -INT "${launch_pid}" 2>/dev/null || true
      fi
    fi
  ' bash "${container_directory}" "${run_key}" >/dev/null 2>&1 || true
  if [[ -n ${client_pid} ]]; then
    for ((attempt=0; attempt<100; ++attempt)); do
      kill -0 "${client_pid}" 2>/dev/null || break
      sleep 0.1
    done
    kill -TERM "${client_pid}" 2>/dev/null || true
    wait "${client_pid}" 2>/dev/null || true
  fi
  rm -f -- "${run_directory}/pid" "${run_directory}/cancel"
  rmdir -- "${run_directory}" 2>/dev/null || true
  exit "${result}"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
docker exec -e "ROBOT_VIDEO_RUN=${run_key}" "${container_id}" bash -c '
  set -e
  [[ ! -e "$1/cancel" ]] || exit 130
  printf "%s\n" "$$" > "$1/pid"
  [[ ! -e "$1/cancel" ]] || exit 130
  shift
  exec /usr/local/bin/robot-entrypoint ros2 launch video_streamer_transmission video.launch.xml "$@"
' bash "${container_directory}" "$@" &
client_pid=$!
wait "${client_pid}"
