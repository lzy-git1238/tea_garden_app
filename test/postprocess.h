#ifndef _RKNN_YOLOV8_POSTPROCESS_H_
#define _RKNN_YOLOV8_POSTPROCESS_H_

#include <stdint.h>
#include <vector>

#define OBJ_NAME_MAX_SIZE     16
#define OBJ_NUMB_MAX_SIZE     128
#define OBJ_CLASS_NUM         6
#define NMS_THRESH            0.45f
#define BOX_THRESH            0.25f
#define DFL_LEN               16         // 64 / 4，YOLOv8 固定

typedef struct _BOX_RECT {
    int left;
    int right;
    int top;
    int bottom;
} BOX_RECT;

typedef struct __detect_result_t {
    char     name[OBJ_NAME_MAX_SIZE];
    BOX_RECT box;
    float    prop;
    int      cls_id;
} detect_result_t;

typedef struct _detect_result_group_t {
    int             id;
    int             count;
    detect_result_t results[OBJ_NUMB_MAX_SIZE];
} detect_result_group_t;

/// 设置标签文件路径（必须在首次调用 post_process 之前调用）
void postprocess_set_label_path(const char *path);

// outputs_buf[9] / output_zps[9] / output_scales[9] 顺序与模型输出 index 一致
int post_process(int8_t  *outputs_buf[9],
                 int32_t  output_zps[9],
                 float    output_scales[9],
                 int      model_in_h, int model_in_w,
                 float    conf_threshold, float nms_threshold,
                 BOX_RECT pads,
                 float    scale_w, float scale_h,
                 detect_result_group_t *group);

void deinitPostProcess();

#endif // _RKNN_YOLOV8_POSTPROCESS_H_
