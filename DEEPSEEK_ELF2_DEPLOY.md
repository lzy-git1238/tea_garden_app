# DeepSeek on ELF2 (RK3588) Deployment Guide

## 1. Target and Assumptions
- Board: ELF2-RK3588 (Linux aarch64)
- Goal: Deploy DeepSeek locally on board for recommendation reasoning
- Runtime: llama.cpp server mode
- Recommended model: DeepSeek-R1-Distill-Qwen-1.5B GGUF (Q4_K_M)

## 2. Install Build Dependencies
```bash
sudo apt update
sudo apt install -y git cmake build-essential python3 python3-pip libopenblas-dev
```

If your system supports Vulkan and drivers are ready:
```bash
sudo apt install -y libvulkan-dev vulkan-tools
```

## 3. Build llama.cpp on ELF2
```bash
cd /opt
sudo git clone https://github.com/ggerganov/llama.cpp.git
cd llama.cpp
sudo cmake -B build -DGGML_OPENMP=ON
sudo cmake --build build -j$(nproc)
```

Optional Vulkan build (only if verified stable on your board image):
```bash
cd /opt/llama.cpp
sudo cmake -B build-vk -DGGML_VULKAN=ON -DGGML_OPENMP=ON
sudo cmake --build build-vk -j$(nproc)
```

## 4. Download DeepSeek GGUF Model
Choose one of the quantized GGUF files and place it in a local model directory:
```bash
sudo mkdir -p /opt/models/deepseek
cd /opt/models/deepseek
# Example: download with huggingface-cli or manual upload via SCP/SFTP
```

Recommended order:
1. deepseek-r1-distill-qwen-1.5b-q4_k_m.gguf
2. deepseek-r1-distill-qwen-7b-q4_k_m.gguf (only if memory is sufficient)

## 5. Start Local Inference Service
CPU baseline:
```bash
/opt/llama.cpp/build/bin/llama-server \
  -m /opt/models/deepseek/deepseek-r1-distill-qwen-1.5b-q4_k_m.gguf \
  -c 2048 \
  -n 512 \
  -t 8 \
  --host 0.0.0.0 \
  --port 18080
```

If Vulkan build is validated, replace binary path with:
```bash
/opt/llama.cpp/build-vk/bin/llama-server
```

## 6. Verify Service Health
```bash
curl http://127.0.0.1:18080/health
```

Example request:
```bash
curl -s http://127.0.0.1:18080/v1/chat/completions \
  -H 'Content-Type: application/json' \
  -d '{
    "messages": [
      {
        "role": "system",
        "content": "你是茶园农艺助手，只输出JSON。"
      },
      {
        "role": "user",
        "content": "土壤湿度18%，气温33C，pH=5.1，叶片图像提示轻度卷曲，请给措施。"
      }
    ],
    "temperature": 0.2,
    "max_tokens": 256
  }'
```

## 7. Production Suggestions
- Use systemd to keep llama-server alive.
- Log prompt/response latency and token usage.
- Add timeout and retry in your Qt/C++ caller.
- Enforce JSON schema in post-processing to avoid malformed advice.
- Fallback policy:
  - If latency too high or OOM: lower context length to 1024
  - If still unstable: switch from 7B to 1.5B model

## 8. Integration Contract for Tea Garden System
The recommendation engine should send:
- sensor snapshot: moisture, temp, humidity, pH, timestamp, device_id
- image summary: disease_risk_score, anomaly tags
- weather context: optional

Expected model output JSON fields:
- irrigation_action
- fertilization_action
- pest_risk_action
- confidence
- reason
