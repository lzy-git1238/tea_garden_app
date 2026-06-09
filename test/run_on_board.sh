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
unset WAYLAND_DISPLAY

# 用 XWayland（X11 兼容层），全屏控制更可靠
export DISPLAY=:0
export QT_QPA_PLATFORM=xcb
export QT_AUTO_SCREEN_SCALE_FACTOR=1
unset DISPLAY

echo "[INFO] 使用 eglfs 全屏模式启动"
echo "[INFO] 屏幕: $(cat /sys/class/graphics/fb0/virtual_size 2>/dev/null || echo '未知')"

exec "${BUILD_DIR}/test" "$@"
