#!/bin/bash
# 在 ELF2 板子上运行程序的启动脚本
# 强制使用 eglfs 全屏渲染，绕过 X11/Wayland

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build-arm-Debug"
if [ ! -f "${BUILD_DIR}/test" ]; then
    BUILD_DIR="${SCRIPT_DIR}"
fi
cd "${BUILD_DIR}" || exit 1

# 清除可能干扰的显示环境变量
unset DISPLAY
unset WAYLAND_DISPLAY
unset QT_QPA_PLATFORM

# 检测显示环境
if [ "$XDG_SESSION_TYPE" = "wayland" ]; then
    export QT_QPA_PLATFORM=wayland
elif [ -z "$XDG_SESSION_TYPE" ]; then
    # SSH/无桌面环境 → eglfs
    export QT_QPA_PLATFORM=eglfs
fi
unset DISPLAY

echo "[INFO] 使用 eglfs 全屏模式启动"
echo "[INFO] 屏幕: $(cat /sys/class/graphics/fb0/virtual_size 2>/dev/null || echo '未知')"

exec "${BUILD_DIR}/test" "$@"
