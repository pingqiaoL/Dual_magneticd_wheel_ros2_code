#!/usr/bin/env bash
# 内部工具：按本次启动 PID 的父子关系清理，兼容 ROS 子进程独立进程组。
set -Eeuo pipefail
python3 - "$@" <<'PYTHON'
import os
import signal
import sys
import time
from pathlib import Path


def processes():
    result = {}
    for process in Path('/proc').iterdir():
        if process.name.isdigit():
            try:
                fields = (process / 'stat').read_text().split(') ', 1)[1].split()
                if fields[0] != 'Z':
                    result[int(process.name)] = (int(fields[1]), fields[19])
            except (FileNotFoundError, ProcessLookupError, PermissionError):
                pass
    return result


snapshot = processes()
selected = {int(value) for value in sys.argv[1:] if value.isdigit()}
while True:
    descendants = {pid for pid, (parent, _) in snapshot.items() if parent in selected}
    if descendants <= selected:
        break
    selected |= descendants
selected.discard(os.getpid())
targets = {pid: snapshot[pid][1] for pid in selected if pid in snapshot}
for pid in sorted(targets, reverse=True):
    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
deadline = time.monotonic() + 15
while targets:
    current = processes()
    targets = {pid: started for pid, started in targets.items()
               if pid in current and current[pid][1] == started}
    if targets and time.monotonic() >= deadline:
        raise SystemExit(f'节点退出超时，请检查 PID: {list(targets)}')
    time.sleep(0.1)
PYTHON
