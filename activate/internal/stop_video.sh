#!/usr/bin/env bash
set -Eeuo pipefail
project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "${project_root}"
if [[ ${1:-} == --help || ${1:-} == -h ]]; then
  echo '用法: bash activate/internal/stop_video.sh；停止本工程视频 launch，相机采集也随之停止。'
  echo 'OpenCV 查看器和测厚进程不受影响。'
  exit 0
fi
if [[ ! -f /.dockerenv && ! -f /run/.containerenv ]]; then
  container_id=$(docker compose ps --status running -q dev)
  [[ -n ${container_id} ]] || exit 0
  exec docker exec "${container_id}" bash /workspace/activate/internal/stop_video.sh
fi

# 只定位明确的 video.launch.xml 启动进程，不按 python/node/container 名称批量杀进程。
python3 - <<'PYTHON'
import os
import signal
import time
from pathlib import Path


def find_launches():
    launches = {}
    for process in Path('/proc').iterdir():
        if not process.name.isdigit():
            continue
        try:
            command = (process / 'cmdline').read_bytes().split(b'\0')
            is_launch = any(command[index:index + 3] == [b'launch', b'video_streamer_transmission',
                                              b'video.launch.xml']
                            for index in range(len(command) - 2))
            is_video_container = (b'__node:=video_container' in command
                                  and Path(os.fsdecode(command[0])).name == 'component_container_mt')
            if is_launch or is_video_container:
                launches[int(process.name)] = (process / 'stat').read_text().split(') ', 1)[1].split()[19]
        except (FileNotFoundError, ProcessLookupError, PermissionError):
            continue
    return launches


def still_running(process_id, start_time):
    try:
        fields = Path(f'/proc/{process_id}/stat').read_text().split(') ', 1)[1].split()
        return fields[0] != 'Z' and fields[19] == start_time
    except (FileNotFoundError, ProcessLookupError):
        return False


launches = find_launches()
for process_id in launches:
    try:
        # 后台 bash 可能继承忽略 SIGINT；ROS launch 同样支持 SIGTERM 正常退出。
        os.kill(process_id, signal.SIGTERM)
    except ProcessLookupError:
        pass
deadline = time.monotonic() + 15
while any(still_running(pid, started) for pid, started in launches.items()):
    if time.monotonic() >= deadline:
        raise SystemExit('视频退出超时；保留现场，请检查原视频终端。')
    time.sleep(0.1)
print('旧视频已停止。' if launches else '当前没有运行中的视频 launch。')
PYTHON
