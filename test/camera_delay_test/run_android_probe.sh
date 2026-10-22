#!/usr/bin/env bash
set -Eeuo pipefail
test_directory=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
device_identifier=${1:-192.168.74.178:5555}
duration_seconds=${2:-75}
[[ ${duration_seconds} =~ ^[1-9][0-9]*$ ]] || exit 2
adb_command="${ANDROID_HOME:-$HOME/Android/Sdk}/platform-tools/adb"
result_directory="${test_directory}/results/android_$(date +%Y%m%d_%H%M%S)"
mkdir -p "${result_directory}"
was_running=$("${adb_command}" -s "${device_identifier}" shell pidof com.climbwheel.mk32 || true)
cleanup()
{
    local result=$?
    trap - EXIT INT TERM
    "${adb_command}" -s "${device_identifier}" pull \
        /data/local/tmp/camera_delay_test/probe.log "${result_directory}/probe.log" || true
    if [[ -n ${was_running} ]]; then
        "${adb_command}" -s "${device_identifier}" shell am start \
            -n com.climbwheel.mk32/.MainActivity > "${result_directory}/restored.log" 2>&1 || true
    fi
    echo "ANDROID_RESULTS=${result_directory}"
    exit "${result}"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
"${adb_command}" -s "${device_identifier}" shell ip address show eth0
"${adb_command}" -s "${device_identifier}" shell am force-stop com.climbwheel.mk32
# Output stays on the controller during sampling, so ADB does not stream video/ACK logs.
"${adb_command}" -s "${device_identifier}" shell \
    "CLASSPATH=/data/local/tmp/camera_delay_test/classes.dex app_process /system/bin VideoArrivalProbe 192.168.144.129 eth0 /dev/ttyHS0 ${duration_seconds} > /data/local/tmp/camera_delay_test/probe.log 2>&1"
