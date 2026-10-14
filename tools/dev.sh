#!/usr/bin/env bash
set -Eeuo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "${root}"
export ROBOT_UID=${ROBOT_UID:-$(id -u)}
export ROBOT_GID=${ROBOT_GID:-$(id -g)}

command -v docker >/dev/null || { echo '请先执行 tools/install_docker_host.sh。' >&2; exit 1; }
docker info >/dev/null 2>&1 || {
  echo '无法访问 Docker。请检查服务状态；加入 docker 组后需要重新连接 SSH。' >&2
  exit 1
}

action=${1:-help}
if [[ $# -gt 0 ]]; then shift; fi
case "${action}" in
  build) docker compose --progress plain build "$@" ;;
  up) docker compose up -d dev ;;
  shell) docker compose exec dev bash ;;
  exec) docker compose exec -T dev /usr/local/bin/robot-entrypoint "$@" ;;
  verify) docker compose exec -T dev /usr/local/bin/robot-entrypoint python3 /workspace/tools/verify_dev_environment.py ;;
  status) docker compose ps ;;
  logs) docker compose logs --tail=100 dev ;;
  down) docker compose down ;;
  *) printf '%s\n' '用法：bash tools/dev.sh {build|up|shell|exec <命令>|verify|status|logs|down}' ;;
esac
