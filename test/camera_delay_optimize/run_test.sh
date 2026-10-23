#!/usr/bin/env bash
set -Eeo pipefail

source /opt/robot-venv/bin/activate
source /opt/ros/jazzy/setup.bash
source /workspace/install/setup.bash
set -u

suite=/workspace/test/camera_delay_optimize
pixel_format=${1:-BGR}
test_seconds=${2:-30}
bitrate_kbps=${3:-6000}
if [[ ${pixel_format} != BGR && ${pixel_format} != I420 && ${pixel_format} != I420_VISION ]]; then
  echo '用法: run_test.sh [BGR|I420|I420_VISION]' >&2
  exit 2
fi
prepare_format=${pixel_format}
vision_argument=()
if [[ ${pixel_format} == I420_VISION ]]; then
  prepare_format=I420
  vision_argument=(--vision-branch)
fi
stamp=$(date +%Y%m%d_%H%M%S)
runtime_root="/workspace/log/camera_delay_optimize_${stamp}_$$"
run_directory="${suite}/results/run_${stamp}"
mkdir -p "${runtime_root}"

python3 "${suite}/prepare_workspace.py" --runtime-root "${runtime_root}" --pixel-format "${prepare_format}"
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
  --bitrate-kbps "${bitrate_kbps}" \
  $([[ ${prepare_format} == I420 ]] && printf '%s' '--only-variant ultrafast_t2') \
  "${vision_argument[@]}" \
  --warmup 5 \
  --seconds "${test_seconds}"

python3 "${suite}/summarize.py" "${run_directory}"
printf 'RESULTS=%s\n' "${run_directory}"
