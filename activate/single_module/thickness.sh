#!/usr/bin/env bash
set -Eeuo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "${project_root}"

if [[ ${1:-} == --help || ${1:-} == -h ]]; then
  cat <<'HELP'
用法（Orin宿主机或开发容器内均可）：
  bash activate/single_module/thickness.sh [thickness_device_path:=/dev/ttyACM0] [thickness_baud_rate:=115200]
    [telemetry_enabled:=true] [telemetry_device_path:=/dev/ttyTHS0]
    [telemetry_baud_rate:=115200]

脚本自动识别环境：宿主机复用/启动开发容器，容器内直接运行XML launch。
每收到有效测量帧打印一条读数；单位统一为 um。Ctrl+C退出。
manager 默认开启结果发布；下位机状态回调仅预留，尚未订阅。
停止本次测厚使用 Ctrl+C；当前没有测厚启停服务。
数传设备须已映射到容器，测厚读数按 um 上传。
修改C++或消息后需先编译；脚本不会每次启动都重新编译。
HELP
  exit 0
fi

launch_arguments=(telemetry_enabled:=true)
for argument in "$@"; do
  case "${argument}" in
    thickness_device_path:=*|thickness_baud_rate:=*) launch_arguments+=("${argument#thickness_}") ;;
    telemetry_*) launch_arguments+=("${argument}") ;;
    *) echo "不支持的参数: ${argument}；使用 --help 查看用法。" >&2; exit 2 ;;
  esac
done
set -- "${launch_arguments[@]}"

# 接受显式列出的启动选项，使用数组原样传递，不拼接执行命令。
for argument in "$@"; do
  case "${argument}" in
    device_path:=/*) ;;
    telemetry_device_path:=/*) ;;
    telemetry_enabled:=true|telemetry_enabled:=false) ;;
    baud_rate:=9600|baud_rate:=19200|baud_rate:=38400|baud_rate:=57600|\
    baud_rate:=115200|baud_rate:=230400|baud_rate:=460800|baud_rate:=921600) ;;
    telemetry_baud_rate:=9600|telemetry_baud_rate:=19200|telemetry_baud_rate:=38400|\
    telemetry_baud_rate:=57600|telemetry_baud_rate:=115200|telemetry_baud_rate:=230400|\
    telemetry_baud_rate:=460800|telemetry_baud_rate:=921600) ;;
    *) echo "不支持的启动选项：${argument}。使用 --help 查看用法。" >&2; exit 2 ;;
  esac
done

command -v flock >/dev/null || { echo '当前环境缺少util-linux提供的flock。' >&2; exit 1; }

# 防止同一工程重复运行脚本，造成两个采集节点争用USB。
mkdir -p "${project_root}/log"
exec 9>"${project_root}/log/.read_thickness.lock"
flock -n 9 || { echo '该工程的测厚读取脚本已经运行，请先结束原来的进程。' >&2; exit 1; }

# VS Code开发容器中的终端没有Docker命令，也不需要再次进入容器。
# 两个环境共享上面的锁；容器内exec之后由ros2 launch处理Ctrl+C和子进程退出。
if [[ -f /.dockerenv || -f /run/.containerenv ]]; then
  if [[ ! -f ${project_root}/install/measurement_manager/share/measurement_manager/launch/thickness.launch.xml ]]; then
    echo '尚未安装XML启动文件，请先在当前容器中编译工作区。' >&2
    exit 1
  fi
  [[ -x /usr/local/bin/robot-entrypoint ]] || {
    echo '当前不是本工程配置的开发容器，缺少robot-entrypoint。' >&2; exit 1;
  }
  exec /usr/local/bin/robot-entrypoint ros2 launch measurement_manager thickness.launch.xml "$@"
fi

command -v docker >/dev/null || { echo '当前宿主机尚未安装Docker。' >&2; exit 1; }
docker info >/dev/null 2>&1 || { echo '无法访问Docker，请检查服务及当前用户权限。' >&2; exit 1; }

export ROBOT_UID=${ROBOT_UID:-$(id -u)}
export ROBOT_GID=${ROBOT_GID:-$(id -g)}
docker compose config --quiet
# VS Code可能使用附加的Compose配置；不要对运行中的容器执行up以免重建。
container_id=$(docker compose ps --status running -q dev)
if [[ -z ${container_id} ]]; then
  docker compose up -d dev
  container_id=$(docker compose ps --status running -q dev)
fi
[[ -n ${container_id} ]] || { echo '开发容器启动失败。' >&2; exit 1; }

if ! docker exec "${container_id}" test -f /workspace/install/measurement_manager/share/measurement_manager/launch/thickness.launch.xml; then
  echo '尚未安装XML启动文件，请先在容器中编译robot_interfaces、thickness_usb_driver和measurement_manager。' >&2
  exit 1
fi

# Docker exec的本地客户端退出不一定会结束容器内进程。
# 记录本次launch的PID与专属标识，退出时只通知本次launch清理两个子节点。
run_key="$$-${RANDOM}"
run_name=".thickness-session-${run_key}"
run_directory="${project_root}/log/${run_name}"
container_run_directory="/workspace/log/${run_name}"
mkdir "${run_directory}"
launch_client_pid=''

cleanup()
{
  local result=$?
  trap - EXIT INT TERM
  # 若启动过程尚未完成，容器端看到此标记后直接取消启动。
  : > "${run_directory}/cancel"
  docker exec "${container_id}" bash -c '
    if [[ -r "$1/pid" ]]; then
      read -r launch_pid < "$1/pid"
      if [[ "${launch_pid}" =~ ^[0-9]+$ && -r "/proc/${launch_pid}/environ" ]] &&
         tr "\0" "\n" < "/proc/${launch_pid}/environ" | grep -Fxq "ROBOT_THICKNESS_RUN=$2"; then
        kill -INT "${launch_pid}" 2>/dev/null || true
      fi
    fi
  ' bash "${container_run_directory}" "${run_key}" >/dev/null 2>&1 || true

  if [[ -n ${launch_client_pid} ]]; then
    # 为ros2 launch的正常退出留出时间，不按进程名称批量终止其他节点。
    for ((attempt=0; attempt<150; ++attempt)); do
      kill -0 "${launch_client_pid}" 2>/dev/null || break
      sleep 0.1
    done
    if kill -0 "${launch_client_pid}" 2>/dev/null; then
      echo 'launch退出超时，请检查容器内节点和Docker连接。' >&2
      kill -TERM "${launch_client_pid}" 2>/dev/null || true
    fi
    wait "${launch_client_pid}" 2>/dev/null || true
  fi
  rm -f -- "${run_directory}/pid" "${run_directory}/cancel"
  rmdir -- "${run_directory}" 2>/dev/null || true
  exit "${result}"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

docker exec -e "ROBOT_THICKNESS_RUN=${run_key}" "${container_id}" bash -c '
  set -e
  run_directory=$1
  shift
  [[ ! -e "$run_directory/cancel" ]] || exit 130
  printf "%s\n" "$$" > "$run_directory/pid"
  [[ ! -e "$run_directory/cancel" ]] || exit 130
  exec /usr/local/bin/robot-entrypoint ros2 launch measurement_manager thickness.launch.xml "$@"
' bash "${container_run_directory}" "$@" &
launch_client_pid=$!

if wait "${launch_client_pid}"; then
  exit 0
else
  exit $?
fi
