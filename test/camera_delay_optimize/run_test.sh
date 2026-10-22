#!/usr/bin/env bash
set -Eeo pipefail

source /opt/robot-venv/bin/activate
source /opt/ros/jazzy/setup.bash
source /workspace/install/setup.bash
set -u

suite=/workspace/test/camera_delay_optimize
stamp=$(date +%Y%m%d_%H%M%S)
runtime_root="/workspace/log/camera_delay_optimize_${stamp}_$$"
run_directory="${suite}/results/run_${stamp}"
mkdir -p "${runtime_root}"

python3 "${suite}/prepare_workspace.py" --runtime-root "${runtime_root}"
build_type=$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' /workspace/build/camera_driver/CMakeCache.txt)
if [[ -z ${build_type} ]]; then
  build_type=Release
fi

cd "${runtime_root}"
colcon build \
  --base-paths src \
  --build-base build \
  --install-base install \
  --packages-select camera_driver video_streamer_transmission \
  --cmake-args -DBUILD_TESTING=OFF "-DCMAKE_BUILD_TYPE=${build_type}"

python3 "${suite}/run_matrix.py" \
  --runtime-root "${runtime_root}" \
  --run-directory "${run_directory}" \
  --width 1920 \
  --height 1080 \
  --frame-rate 30 \
  --bitrates-kbps 6000 8000 \
  --warmup 5 \
  --seconds 30

python3 "${suite}/summarize.py" "${run_directory}"
printf 'RESULTS=%s\n' "${run_directory}"
