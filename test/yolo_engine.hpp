#ifndef SLAM_CAR_YOLO__YOLO_ENGINE_HPP_
#define SLAM_CAR_YOLO__YOLO_ENGINE_HPP_

#include <string>
#include <vector>
#include <mutex>
#include <opencv2/core.hpp>

#include "rknn_api.h"
#include "postprocess.h"

namespace slam_car_yolo {

/// 单个检测结果（图像坐标系，像素单位）
struct Detection {
    int cls_id;
    float confidence;
    int left, top, right, bottom;
    const char *name;  // 指向标签字符串，生命周期 = engine
};

/// YOLOv8 RKNN 推理引擎
/// 线程安全：init() 调用一次，infer() 可用锁保护后多线程调用
class YoloEngine {
public:
    YoloEngine();
    ~YoloEngine();

    /// 初始化模型，加载 .rknn 并设置 NPU 核心
    /// @param model_path  .rknn 文件路径
    /// @param labels_path 标签文件路径（一行一类别）
    /// @param core_mask   NPU 核心掩码: 1=Core0, 2=Core1, 4=Core2, 7=三核全开
    /// @return 成功返回 true
    bool init(const std::string &model_path,
              const std::string &labels_path,
              int core_mask = 1);

    /// 对单张图像做检测（线程安全）
    /// @param image    输入 BGR 图像（会被原地画框覆盖）
    /// @param detections 输出检测结果
    /// @param draw     是否在 image 上绘制检测框
    /// @return 成功返回 true
    bool infer(cv::Mat &image,
               std::vector<Detection> &detections,
               bool draw = true);

    /// 设置检测阈值
    void setConfThreshold(float v) { conf_threshold_ = v; }
    void setNmsThreshold(float v) { nms_threshold_ = v; }

    int modelWidth()  const { return model_width_; }
    int modelHeight() const { return model_height_; }

private:
    bool loadModel(const std::string &path);
    bool initRuntime(int core_mask);
    bool queryModelInfo();
    void preprocess(const cv::Mat &image, cv::Mat &resized, float &scale,
                    BOX_RECT &pads);

    rknn_context ctx_;
    unsigned char *model_data_{nullptr};
    int model_size_{0};
    bool initialized_{false};

    rknn_input_output_num io_num_;
    rknn_tensor_attr *input_attrs_{nullptr};
    rknn_tensor_attr *output_attrs_{nullptr};

    int model_width_{640};
    int model_height_{640};
    int model_channel_{3};

    float conf_threshold_{0.25f};
    float nms_threshold_{0.45f};

    std::mutex infer_mutex_;
};

}  // namespace slam_car_yolo

#endif  // SLAM_CAR_YOLO__YOLO_ENGINE_HPP_
