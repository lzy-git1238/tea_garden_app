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

# 天气预警 API（星图云开放平台）
export TEA_WEATHER_TOKEN="你的token填这里"
export TEA_WEATHER_LOCATION="WTX_CH101280800"

exec ./test "$@"
