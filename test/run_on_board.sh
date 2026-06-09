#!/bin/bash
# 在 ELF2 板子上运行程序的启动脚本
# 强制使用 eglfs 全屏渲染，绕过 X11/Wayland

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "${SCRIPT_DIR}" || exit 1

# 清除可能干扰的显示环境变量
unset DISPLAY
unset WAYLAND_DISPLAY
unset QT_QPA_PLATFORM

# 强制 eglfs（DRM/KMS 直接渲染，全屏无标题栏）
export QT_QPA_PLATFORM=eglfs

echo "[INFO] 使用 eglfs 全屏模式启动"
echo "[INFO] 屏幕: $(cat /sys/class/graphics/fb0/virtual_size 2>/dev/null || echo '未知')"

exec ./test "$@"
