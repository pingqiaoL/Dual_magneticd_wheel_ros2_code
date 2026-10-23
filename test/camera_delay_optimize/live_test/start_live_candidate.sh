#!/usr/bin/env bash
set -euo pipefail

name="${1:?candidate name required}"
options="${2:?x264 options required}"
launch_file="${3:-/workspace/test/camera_delay_optimize/live_test/gop10.launch.xml}"

old="$(pgrep -f 'ros2 launch /workspace/test/camera_delay_optimize/live_test/' | head -1 || true)"
if [[ -n "${old}" ]]; then
  bash /workspace/activate/internal/stop_launch_tree.sh "${old}"
fi

result="/workspace/test/camera_delay_optimize/live_test/${name}"
rm -rf "${result}"
mkdir -p "${result}"
nohup bash -lc "
  source /workspace/log/camera_delay_optimize_20261023_142519_13/install/setup.bash
  export CAMERA_DELAY_OUTPUT='${result}'
  export CAMERA_DELAY_X264_OPTIONS='${options}'
  export ROS_DOMAIN_ID=79
  exec ros2 launch '${launch_file}'
" > "/workspace/test/camera_delay_optimize/live_test/video_${name}.log" 2>&1 &
echo "$!" > /workspace/test/camera_delay_optimize/live_test/video.pid

for _ in $(seq 1 150); do
  if grep -q 'RTSP :8554/live' "/workspace/test/camera_delay_optimize/live_test/video_${name}.log"; then
    cat /workspace/test/camera_delay_optimize/live_test/video.pid
    tail -12 "/workspace/test/camera_delay_optimize/live_test/video_${name}.log"
    exit 0
  fi
  sleep 0.1
done

cat "/workspace/test/camera_delay_optimize/live_test/video_${name}.log"
exit 1
