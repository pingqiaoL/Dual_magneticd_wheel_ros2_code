#!/usr/bin/env bash
set -Eeuo pipefail
project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
device_path=${1:-/dev/video0}
if [[ ! -f /.dockerenv && ! -f /run/.containerenv ]]; then
  exec docker exec robot_ros2_dev bash /workspace/tools/inspect_camera.sh "${device_path}"
fi
command -v v4l2-ctl >/dev/null || { echo '缺少 v4l-utils；请按视频联调文档安装依赖。' >&2; exit 1; }
v4l2-ctl --list-devices
v4l2-ctl -d "${device_path}" --list-formats-ext
for plugin in v4l2src jpegdec videoconvert appsink appsrc x264enc h264parse rtph264pay; do
  gst-inspect-1.0 "${plugin}" >/dev/null
  echo "插件可用: ${plugin}"
done
