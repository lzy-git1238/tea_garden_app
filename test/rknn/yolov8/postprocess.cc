#include "postprocess.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <set>
#include <vector>

static char *labels[OBJ_CLASS_NUM] = {nullptr};

static char *readLine(FILE *fp) {
    int ch, i = 0;
    size_t cap = 0;
    char *buf = (char *)malloc(cap + 1);
    if (!buf) return nullptr;
    while ((ch = fgetc(fp)) != '\n' && ch != EOF) {
        cap++;
        char *tmp = (char *)realloc(buf, cap + 1);
        if (!tmp) { free(buf); return nullptr; }
        buf = tmp;
        buf[i++] = (char)ch;
    }
    buf[i] = '\0';
    if (ch == EOF && i == 0) { free(buf); return nullptr; }
    return buf;
}

static int loadLabelName(const char *fname, char *lbl[]) {
    FILE *f = fopen(fname, "r");
    if (!f) { printf("Open %s fail!\n", fname); return -1; }
    int i = 0; char *s;
    while ((s = readLine(f)) != nullptr) {
        lbl[i++] = s;
        if (i >= OBJ_CLASS_NUM) break;
    }
    fclose(f);
    return i;
}

static const char *cls_to_name(int cls_id) {
    if (cls_id < 0 || cls_id >= OBJ_CLASS_NUM) return "null";
    return labels[cls_id] ? labels[cls_id] : "null";
}

inline static int clamp(float v, int lo, int hi) {
    return v > lo ? (v < hi ? (int)v : hi) : lo;
}

static float CalculateOverlap(float xmin0, float ymin0, float xmax0, float ymax0,
                              float xmin1, float ymin1, float xmax1, float ymax1) {
    float w = fmaxf(0.f, fminf(xmax0, xmax1) - fmaxf(xmin0, xmin1) + 1.0f);
    float h = fmaxf(0.f, fminf(ymax0, ymax1) - fmaxf(ymin0, ymin1) + 1.0f);
    float i = w * h;
    float u = (xmax0 - xmin0 + 1.0f) * (ymax0 - ymin0 + 1.0f) +
              (xmax1 - xmin1 + 1.0f) * (ymax1 - ymin1 + 1.0f) - i;
    return u <= 0.f ? 0.f : (i / u);
}

static int nms(int validCount, std::vector<float> &locs, std::vector<int> &cls_ids,
               std::vector<int> &order, int filterId, float thresh) {
    for (int i = 0; i < validCount; ++i) {
        int n = order[i];
        if (n == -1 || cls_ids[n] != filterId) continue;
        float x0 = locs[n*4+0], y0 = locs[n*4+1];
        float x1 = locs[n*4+0] + locs[n*4+2];
        float y1 = locs[n*4+1] + locs[n*4+3];
        for (int j = i + 1; j < validCount; ++j) {
            int m = order[j];
            if (m == -1 || cls_ids[m] != filterId) continue;
            float x2 = locs[m*4+0], y2 = locs[m*4+1];
            float x3 = locs[m*4+0] + locs[m*4+2];
            float y3 = locs[m*4+1] + locs[m*4+3];
            if (CalculateOverlap(x0, y0, x1, y1, x2, y2, x3, y3) > thresh) order[j] = -1;
        }
    }
    return 0;
}

static int quick_sort_indice_inverse(std::vector<float> &input, int left, int right,
                                     std::vector<int> &indices) {
    if (left >= right) return 0;
    float key = input[left];
    int key_idx = indices[left];
    int low = left, high = right;
    while (low < high) {
        while (low < high && input[high] <= key) high--;
        input[low] = input[high]; indices[low] = indices[high];
        while (low < high && input[low] >= key) low++;
        input[high] = input[low]; indices[high] = indices[low];
    }
    input[low] = key; indices[low] = key_idx;
    quick_sort_indice_inverse(input, left, low - 1, indices);
    quick_sort_indice_inverse(input, low + 1, right, indices);
    return 0;
}

static inline float sigmoid_f(float x)   { return 1.0f / (1.0f + expf(-x)); }
static inline float unsigmoid_f(float y) { return -logf((1.0f / y) - 1.0f); }
inline static int32_t __clip(float v, float lo, float hi) {
    return (int32_t)(v <= lo ? lo : (v >= hi ? hi : v));
}
static inline int8_t qnt_f32_to_i8(float v, int32_t zp, float scale) {
    return (int8_t)__clip((v / scale) + zp, -128.f, 127.f);
}
static inline float deqnt_i8_to_f32(int8_t q, int32_t zp, float scale) {
    return ((float)q - (float)zp) * scale;
}

// DFL: 把 (4*dfl_len) 个 logit 经 softmax 加权求期望得到 4 条边距离
static void compute_dfl(const float *tensor, int dfl_len, float box[4]) {
    for (int b = 0; b < 4; b++) {
        float exp_t[DFL_LEN], exp_sum = 0.f, acc = 0.f;
        for (int i = 0; i < dfl_len; i++) { exp_t[i] = expf(tensor[i + b*dfl_len]); exp_sum += exp_t[i]; }
        for (int i = 0; i < dfl_len; i++) acc += exp_t[i] / exp_sum * i;
        box[b] = acc;
    }
}

// 单 stride 分支: box[1,64,H,W] / score[1,C,H,W] / sum[1,1,H,W]
static int process_i8(int8_t *box_t,   int32_t box_zp,   float box_scale,
                      int8_t *score_t, int32_t score_zp, float score_scale,
                      int8_t *sum_t,   int32_t sum_zp,   float sum_scale,
                      int grid_h, int grid_w, int stride, int dfl_len,
                      std::vector<float> &boxes, std::vector<float> &probs,
                      std::vector<int> &classId, float threshold)
{
    const int grid_len = grid_h * grid_w;
    int8_t sum_thres_i8   = qnt_f32_to_i8(threshold, sum_zp, sum_scale);
    int8_t score_thres_i8 = qnt_f32_to_i8(unsigmoid_f(threshold), score_zp, score_scale);
    int valid = 0;
    for (int i = 0; i < grid_h; i++) {
        for (int j = 0; j < grid_w; j++) {
            int offset = i * grid_w + j;
            // 1) score_sum 快速过滤
            if (sum_t != nullptr && sum_t[offset] < sum_thres_i8) continue;
            // 2) 选最大类
            int8_t max_score = score_thres_i8; int max_cls = -1; int cls_off = offset;
            for (int c = 0; c < OBJ_CLASS_NUM; c++) {
                if (score_t[cls_off] > max_score) { max_score = score_t[cls_off]; max_cls = c; }
                cls_off += grid_len;
            }
            if (max_cls < 0) continue;
            // 3) DFL bbox 解码
            float before_dfl[DFL_LEN * 4]; int box_off = offset;
            for (int k = 0; k < dfl_len * 4; k++) { before_dfl[k] = deqnt_i8_to_f32(box_t[box_off], box_zp, box_scale); box_off += grid_len; }
            float bx[4]; compute_dfl(before_dfl, dfl_len, bx);
            float x1 = (-bx[0] + j + 0.5f) * stride, y1 = (-bx[1] + i + 0.5f) * stride;
            float x2 = ( bx[2] + j + 0.5f) * stride, y2 = ( bx[3] + i + 0.5f) * stride;
            boxes.push_back(x1); boxes.push_back(y1); boxes.push_back(x2 - x1); boxes.push_back(y2 - y1);
            probs.push_back(sigmoid_f(deqnt_i8_to_f32(max_score, score_zp, score_scale)));
            classId.push_back(max_cls);
            valid++;
        }
    }
    return valid;
}

int post_process(int8_t  *outputs_buf[9], int32_t output_zps[9], float output_scales[9],
                 int model_in_h, int model_in_w,
                 float conf_threshold, float nms_threshold,
                 BOX_RECT pads, float scale_w, float scale_h,
                 detect_result_group_t *group)
{
    static bool labels_loaded = false;
    if (!labels_loaded) { loadLabelName(LABEL_NALE_TXT_PATH, labels); labels_loaded = true; }
    memset(group, 0, sizeof(detect_result_group_t));

    std::vector<float> filterBoxes, probs;
    std::vector<int>   classId;
    const int strides[3] = {8, 16, 32};
    for (int s = 0; s < 3; s++) {
        int bi = s*3+0, si = s*3+1, ui = s*3+2, stride = strides[s];
        int gh = model_in_h / stride, gw = model_in_w / stride;
        process_i8(outputs_buf[bi], output_zps[bi], output_scales[bi],
                   outputs_buf[si], output_zps[si], output_scales[si],
                   outputs_buf[ui], output_zps[ui], output_scales[ui],
                   gh, gw, stride, DFL_LEN, filterBoxes, probs, classId, conf_threshold);
    }

    int validCount = (int)probs.size();
    if (validCount <= 0) { group->count = 0; return 0; }
    std::vector<int> order(validCount);
    for (int i = 0; i < validCount; i++) order[i] = i;
    quick_sort_indice_inverse(probs, 0, validCount - 1, order);
    std::set<int> class_set(classId.begin(), classId.end());
    for (int c : class_set) nms(validCount, filterBoxes, classId, order, c, nms_threshold);

    int last = 0;
    for (int i = 0; i < validCount; i++) {
        if (order[i] == -1 || last >= OBJ_NUMB_MAX_SIZE) continue;
        int n = order[i];
        float x1 = filterBoxes[n*4+0] - pads.left;
        float y1 = filterBoxes[n*4+1] - pads.top;
        float x2 = x1 + filterBoxes[n*4+2];
        float y2 = y1 + filterBoxes[n*4+3];
        group->results[last].box.left   = clamp(x1, 0, model_in_w) / scale_w;
        group->results[last].box.top    = clamp(y1, 0, model_in_h) / scale_h;
        group->results[last].box.right  = clamp(x2, 0, model_in_w) / scale_w;
        group->results[last].box.bottom = clamp(y2, 0, model_in_h) / scale_h;
        group->results[last].prop       = probs[i];
        group->results[last].cls_id     = classId[n];
        strncpy(group->results[last].name, cls_to_name(classId[n]), OBJ_NAME_MAX_SIZE - 1);
        group->results[last].name[OBJ_NAME_MAX_SIZE - 1] = '\0';
        last++;
    }
    group->count = last;
    return 0;
}

void deinitPostProcess() {
    for (int i = 0; i < OBJ_CLASS_NUM; i++)
        if (labels[i]) { free(labels[i]); labels[i] = nullptr; }
}