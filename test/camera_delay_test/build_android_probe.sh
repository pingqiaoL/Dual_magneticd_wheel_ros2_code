#!/usr/bin/env bash
set -Eeuo pipefail
test_directory=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
sdk_directory=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}
mkdir -p "${test_directory}/build/classes" "${test_directory}/build/dex"
javac --release 8 -d "${test_directory}/build/classes" "${test_directory}/VideoArrivalProbe.java"
jar cf "${test_directory}/build/probe.jar" -C "${test_directory}/build/classes" .
"${sdk_directory}/build-tools/33.0.2/d8" \
    --lib "${sdk_directory}/platforms/android-33/android.jar" \
    --min-api 23 --output "${test_directory}/build/dex" "${test_directory}/build/probe.jar"
echo "DEX=${test_directory}/build/dex/classes.dex"
