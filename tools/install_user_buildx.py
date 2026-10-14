#!/usr/bin/env python3
"""Install Ubuntu's signed-repository Buildx package into this user's plugin directory."""
from pathlib import Path
import hashlib
import json
import subprocess

probe = subprocess.run(["docker", "buildx", "version"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
if probe.returncode == 0:
    print(probe.stdout.decode().strip())
    raise SystemExit(0)

cache = Path.home() / ".cache/robot_ros2/buildx"
cache.mkdir(parents=True, exist_ok=True)
metadata = subprocess.check_output(["apt-cache", "show", "docker-buildx"], text=True).split("\n\n")[0]
fields = dict(line.split(": ", 1) for line in metadata.splitlines() if ": " in line and not line.startswith(" "))
version = fields["Version"]
subprocess.run(["apt-get", "download", "docker-buildx=" + version], cwd=cache, check=True)
package = cache / Path(fields["Filename"]).name
if hashlib.sha256(package.read_bytes()).hexdigest() != fields["SHA256"]:
    raise RuntimeError("Buildx 软件包与 APT 元数据校验值不符")
unpacked = cache / ("unpacked-" + version)
subprocess.run(["dpkg-deb", "--extract", str(package), str(unpacked)], check=True)
binaries = [path for path in unpacked.rglob("docker-buildx") if path.is_file()]
if len(binaries) != 1:
    raise RuntimeError("Buildx 包中的插件路径不唯一")
destination = Path.home() / ".docker/cli-plugins/docker-buildx"
destination.parent.mkdir(parents=True, exist_ok=True)
if destination.exists() or destination.is_symlink():
    raise RuntimeError("用户插件路径已存在，保留现有文件并停止")
destination.symlink_to(binaries[0])
subprocess.run(["docker", "buildx", "version"], check=True)
print(json.dumps({"user_plugin": str(destination), "ubuntu_package_version": version}))
