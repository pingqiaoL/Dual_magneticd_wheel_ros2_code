#!/usr/bin/env bash
set -Eeuo pipefail
suite_directory=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
if [[ ! -f /.dockerenv && ! -f /run/.containerenv ]]; then
    echo '请先在 Orin 宿主机执行 docker exec -it robot_ros2_dev bash，再运行本脚本。' >&2
    exit 1
fi
if [[ ${1:-} == --help || ${1:-} == -h ]]; then
    echo '用法: bash /workspace/test/camera_delay_test/run_test.sh'
    echo '独立构建测试副本；5 组测试各预热 10 秒、采集 60 秒，结束恢复原组合入口和查看器。'
    echo '需要当前桌面 DISPLAY 可用。结果保存在本目录 results/时间戳/。'
    exit 0
fi
source /opt/ros/jazzy/setup.bash
source /workspace/install/setup.bash
runtime_root="/workspace/log/camera_delay_build_$(date +%Y%m%d_%H%M%S)_$$"
mkdir -p "${runtime_root}"
python3 "${suite_directory}/prepare_workspace.py" --runtime-root "${runtime_root}"
# 使用正式 camera_driver 的构建类型，避免优化级别影响对照。
build_type=$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' /workspace/build/camera_driver/CMakeCache.txt)
cd "${runtime_root}"
colcon build --base-paths src --build-base build --install-base install \
    --packages-select camera_driver video_streamer_transmission \
    --cmake-args -DBUILD_TESTING=OFF "-DCMAKE_BUILD_TYPE=${build_type}"
run_directory="${suite_directory}/results/$(date +%Y%m%d_%H%M%S)"
python3 "${suite_directory}/run_matrix.py" --runtime-root "${runtime_root}" \
    --run-directory "${run_directory}" --warmup 10 --seconds 60
python3 "${suite_directory}/analyze.py" "${run_directory}"
echo "测试与汇总完成：${run_directory}/SUMMARY.md"
