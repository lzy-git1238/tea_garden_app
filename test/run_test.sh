#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

if [[ -f /opt/ros/humble/setup.bash ]]; then
  # shellcheck disable=SC1091
  source /opt/ros/humble/setup.bash
fi

export QT_QPA_PLATFORM="wayland;xcb"

if [[ -z "${WAYLAND_DISPLAY:-}" && -n "${XDG_RUNTIME_DIR:-}" ]]; then
  export WAYLAND_DISPLAY="wayland-0"
fi

if [[ -z "${DISPLAY:-}" ]]; then
  export DISPLAY=":0"
fi

if [[ "$(id -u)" == "0" ]]; then
  export QTWEBENGINE_DISABLE_SANDBOX=1
fi

exec ./test "$@"
