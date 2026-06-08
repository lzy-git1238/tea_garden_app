#!/bin/bash
# YOLO RKNN 推理服务启动脚本
# 解决 rknn_env 虚拟环境依赖问题
#
# 用法:
#   ./start_yolo_server.sh                    # 使用默认配置
#   YOLO_PORT=9801 ./start_yolo_server.sh     # 指定端口
#
# 部署到 ELF2: ~/sdcard/start_yolo_server.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VENV_DIR="${SCRIPT_DIR}/rknn_env"
MODEL_DIR="${SCRIPT_DIR}/models"
PORT="${YOLO_PORT:-9800}"

# 检查虚拟环境
if [ ! -d "$VENV_DIR" ]; then
    echo "[ERROR] 虚拟环境不存在: $VENV_DIR"
    echo "[INFO] 请先创建: python3 -m venv $VENV_DIR"
    echo "[INFO] 然后安装: source $VENV_DIR/bin/activate && pip install rknn-api numpy opencv-python"
    exit 1
fi

# 激活虚拟环境
source "${VENV_DIR}/bin/activate"

# 检查模型文件
RKNN_MODEL="${MODEL_DIR}/yolov5s_last.rknn"
if [ ! -f "$RKNN_MODEL" ]; then
    echo "[ERROR] 模型文件不存在: $RKNN_MODEL"
    echo "[INFO] 请将 yolov5s_last.rknn 放到 $MODEL_DIR/ 目录下"
    exit 1
fi

echo "========================================="
echo "  YOLO RKNN 推理服务"
echo "========================================="
echo "[INFO] 虚拟环境: $VENV_DIR"
echo "[INFO] 模型文件: $RKNN_MODEL"
echo "[INFO] 监听端口: $PORT"
echo "[INFO] 按 Ctrl+C 停止服务"
echo "========================================="

# 启动推理服务（在虚拟环境下运行）
exec python "${SCRIPT_DIR}/rknn_yolo_server.py" \
    --port "$PORT" \
    --model "$RKNN_MODEL"
