#include "yolo_engine.hpp"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <opencv2/imgproc.hpp>

namespace slam_car_yolo {

// ============================================================
// 静态工具函数
// ============================================================

static unsigned char *load_data(FILE *fp, size_t ofst, size_t sz) {
    if (!fp) return nullptr;
    if (fseek(fp, ofst, SEEK_SET) != 0) return nullptr;
    auto *data = (unsigned char *)malloc(sz);
    if (!data) return nullptr;
    fread(data, 1, sz, fp);
    return data;
}

static unsigned char *load_file(const char *filename, int *size_out) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "[YoloEngine] Cannot open model: %s\n", filename);
        return nullptr;
    }
    fseek(fp, 0, SEEK_END);
    int size = ftell(fp);
    auto *data = load_data(fp, 0, size);
    fclose(fp);
    *size_out = size;
    return data;
}

// ============================================================
// YoloEngine 实现
// ============================================================

YoloEngine::YoloEngine() {
    memset(&ctx_, 0, sizeof(ctx_));
    memset(&io_num_, 0, sizeof(io_num_));
}

YoloEngine::~YoloEngine() {
    if (initialized_) {
        deinitPostProcess();
        rknn_destroy(ctx_);
    }
    free(model_data_);
    free(input_attrs_);
    free(output_attrs_);
}

bool YoloEngine::init(const std::string &model_path,
                      const std::string &labels_path,
                      int core_mask) {
    // 1. 加载模型文件
    if (!loadModel(model_path)) return false;

    // 2. 设置标签路径
    postprocess_set_label_path(labels_path.c_str());
    printf("[YoloEngine] Labels: %s\n", labels_path.c_str());

    // 3. 初始化 RKNN runtime
    if (!initRuntime(core_mask)) return false;

    // 4. 查询模型 I/O 信息
    if (!queryModelInfo()) return false;

    initialized_ = true;
    printf("[YoloEngine] Init done. Input: %dx%dx%d\n",
           model_width_, model_height_, model_channel_);
    return true;
}

bool YoloEngine::loadModel(const std::string &path) {
    printf("[YoloEngine] Loading model: %s\n", path.c_str());
    model_data_ = load_file(path.c_str(), &model_size_);
    if (!model_data_) return false;
    printf("[YoloEngine] Model size: %d bytes\n", model_size_);
    return true;
}

bool YoloEngine::initRuntime(int core_mask) {
    int ret = rknn_init(&ctx_, model_data_, model_size_, 0, nullptr);
    if (ret < 0) {
        fprintf(stderr, "[YoloEngine] rknn_init failed, ret=%d\n", ret);
        return false;
    }

    // 绑定 NPU 核心
    ret = rknn_set_core_mask(ctx_, (rknn_core_mask)core_mask);
    if (ret < 0) {
        fprintf(stderr, "[YoloEngine] rknn_set_core_mask failed, ret=%d\n", ret);
        return false;
    }

    rknn_sdk_version version;
    ret = rknn_query(ctx_, RKNN_QUERY_SDK_VERSION, &version,
                     sizeof(rknn_sdk_version));
    if (ret < 0) {
        fprintf(stderr, "[YoloEngine] query sdk version failed\n");
        return false;
    }
    printf("[YoloEngine] SDK: %s  Driver: %s  CoreMask: %d\n",
           version.api_version, version.drv_version, core_mask);
    return true;
}

bool YoloEngine::queryModelInfo() {
    int ret = rknn_query(ctx_, RKNN_QUERY_IN_OUT_NUM, &io_num_, sizeof(io_num_));
    if (ret < 0) {
        fprintf(stderr, "[YoloEngine] query in/out num failed\n");
        return false;
    }
    printf("[YoloEngine] Inputs: %d  Outputs: %d\n",
           io_num_.n_input, io_num_.n_output);

    // 输入属性
    input_attrs_ = (rknn_tensor_attr *)calloc(io_num_.n_input,
                                              sizeof(rknn_tensor_attr));
    for (int i = 0; i < io_num_.n_input; i++) {
        input_attrs_[i].index = i;
        ret = rknn_query(ctx_, RKNN_QUERY_INPUT_ATTR, &input_attrs_[i],
                         sizeof(rknn_tensor_attr));
        if (ret < 0) return false;
    }

    // 输出属性
    output_attrs_ = (rknn_tensor_attr *)calloc(io_num_.n_output,
                                               sizeof(rknn_tensor_attr));
    for (int i = 0; i < io_num_.n_output; i++) {
        output_attrs_[i].index = i;
        ret = rknn_query(ctx_, RKNN_QUERY_OUTPUT_ATTR, &output_attrs_[i],
                         sizeof(rknn_tensor_attr));
        if (ret < 0) return false;
    }

    // 解析输入尺寸（兼容 NCHW 和 NHWC）
    if (input_attrs_[0].fmt == RKNN_TENSOR_NCHW) {
        model_channel_ = input_attrs_[0].dims[1];
        model_height_  = input_attrs_[0].dims[2];
        model_width_   = input_attrs_[0].dims[3];
    } else {
        // NHWC (YOLOv8 通常用这个)
        model_height_  = input_attrs_[0].dims[1];
        model_width_   = input_attrs_[0].dims[2];
        model_channel_ = input_attrs_[0].dims[3];
    }
    return true;
}

void YoloEngine::preprocess(const cv::Mat &image, cv::Mat &resized,
                            float &scale, BOX_RECT &pads) {
    // 1. BGR → RGB
    cv::Mat rgb;
    cv::cvtColor(image, rgb, cv::COLOR_BGR2RGB);

    // 2. 计算缩放比例（保持宽高比）
    float scale_w = (float)model_width_ / rgb.cols;
    float scale_h = (float)model_height_ / rgb.rows;
    scale = std::min(scale_w, scale_h);

    int new_w = (int)(rgb.cols * scale);
    int new_h = (int)(rgb.rows * scale);

    // 3. 缩放到目标尺寸内
    cv::Mat scaled;
    cv::resize(rgb, scaled, cv::Size(new_w, new_h), 0, 0, cv::INTER_LINEAR);

    // 4. letterbox 填充到 640×640
    pads.left   = (model_width_ - new_w) / 2;
    pads.right  = model_width_ - new_w - pads.left;
    pads.top    = (model_height_ - new_h) / 2;
    pads.bottom = model_height_ - new_h - pads.top;

    cv::copyMakeBorder(scaled, resized,
                       pads.top, pads.bottom,
                       pads.left, pads.right,
                       cv::BORDER_CONSTANT,
                       cv::Scalar(114, 114, 114));
}

bool YoloEngine::infer(cv::Mat &image,
                       std::vector<Detection> &detections,
                       bool draw) {
    detections.clear();
    if (!initialized_) return false;

    std::lock_guard<std::mutex> lock(infer_mutex_);

    // ---- 1. 前处理 ----
    cv::Mat resized;
    float scale;
    BOX_RECT pads;
    preprocess(image, resized, scale, pads);

    // ---- 2. 设置输入 ----
    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index        = 0;
    inputs[0].type         = RKNN_TENSOR_UINT8;
    inputs[0].size         = model_width_ * model_height_ * model_channel_;
    inputs[0].fmt          = RKNN_TENSOR_NHWC;
    inputs[0].buf          = resized.data;
    inputs[0].pass_through = 0;

    int ret = rknn_inputs_set(ctx_, io_num_.n_input, inputs);
    if (ret < 0) {
        fprintf(stderr, "[YoloEngine] rknn_inputs_set failed\n");
        return false;
    }

    // ---- 3. 推理 ----
    ret = rknn_run(ctx_, nullptr);
    if (ret < 0) {
        fprintf(stderr, "[YoloEngine] rknn_run failed\n");
        return false;
    }

    // ---- 4. 获取输出 ----
    rknn_output outputs[io_num_.n_output];
    memset(outputs, 0, sizeof(outputs));
    for (int i = 0; i < io_num_.n_output; i++) {
        outputs[i].want_float = 0;  // INT8 输出
    }
    ret = rknn_outputs_get(ctx_, io_num_.n_output, outputs, nullptr);
    if (ret < 0) {
        fprintf(stderr, "[YoloEngine] rknn_outputs_get failed\n");
        return false;
    }

    // ---- 5. 后处理 ----
    int8_t  *bufs[9]   = {nullptr};
    int32_t  zps[9]    = {0};
    float    scales[9] = {0};
    int nout = io_num_.n_output < 9 ? io_num_.n_output : 9;
    for (int i = 0; i < nout; i++) {
        bufs[i]   = (int8_t *)outputs[i].buf;
        zps[i]    = output_attrs_[i].zp;
        scales[i] = output_attrs_[i].scale;
    }

    detect_result_group_t group;
    post_process(bufs, zps, scales, model_height_, model_width_,
                 conf_threshold_, nms_threshold_, pads,
                 scale, scale, &group);

    // ---- 6. 转换 / 绘框 ----
    for (int i = 0; i < group.count; i++) {
        detect_result_t &r = group.results[i];
        detections.push_back({
            r.cls_id,
            r.prop,
            r.box.left,
            r.box.top,
            r.box.right,
            r.box.bottom,
            r.name
        });
    }

    if (draw && group.count > 0) {
        static const cv::Scalar CLASS_COLORS[] = {
            cv::Scalar(0, 0, 255),    // 0  红
            cv::Scalar(0, 255, 0),    // 1  绿
            cv::Scalar(255, 0, 0),    // 2  蓝
            cv::Scalar(0, 255, 255),  // 3  黄
            cv::Scalar(255, 0, 255),  // 4  品红
            cv::Scalar(255, 255, 0),  // 5  青
            cv::Scalar(0, 128, 255),  // 6  橙
            cv::Scalar(128, 0, 255),  // 7  玫红
        };
        const int NUM_COLORS = sizeof(CLASS_COLORS) / sizeof(CLASS_COLORS[0]);

        for (int i = 0; i < group.count; i++) {
            detect_result_t &r = group.results[i];
            cv::Scalar color = CLASS_COLORS[r.cls_id % NUM_COLORS];
            cv::rectangle(image,
                cv::Point(r.box.left, r.box.top),
                cv::Point(r.box.right, r.box.bottom), color, 2);

            char text[256];
            snprintf(text, sizeof(text), "%s %.1f%%", r.name, r.prop * 100);

            int baseline = 0;
            cv::Size tsize = cv::getTextSize(
                text, cv::FONT_HERSHEY_SIMPLEX, 0.6, 1, &baseline);
            int ty = (r.box.top - 5 < tsize.height)
                         ? (r.box.top + tsize.height + 5)
                         : (r.box.top - 5);

            cv::rectangle(image,
                cv::Point(r.box.left, ty - tsize.height - 4),
                cv::Point(r.box.left + tsize.width + 2, ty + baseline - 2),
                color, cv::FILLED);
            cv::putText(image, text,
                cv::Point(r.box.left + 1, ty - 2),
                cv::FONT_HERSHEY_SIMPLEX, 0.6,
                cv::Scalar(255, 255, 255), 1);
        }
    }

    // ---- 7. 释放输出 ----
    rknn_outputs_release(ctx_, io_num_.n_output, outputs);

    return true;
}

}  // namespace slam_car_yolo
