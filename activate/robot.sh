#!/usr/bin/env bash
set -Eeuo pipefail
activate_directory=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
exec bash "${activate_directory}/mix_module/camera_thickness.sh" "$@"
