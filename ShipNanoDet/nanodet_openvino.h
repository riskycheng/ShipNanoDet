//
// Create by RangiLyu
// 2021 / 1 / 12
//

#ifndef _NANODET_OPENVINO_H_
#define _NANODET_OPENVINO_H_

#include <string>
#include <opencv2/core.hpp>
#include <inference_engine.hpp>


struct  AppConfig_ {
    // the input info, do not need to parse from json, just for external usage
    int input_width = 0;
    int input_height = 0;

    // the warning line points
    int x1 = 300;
    int y1 = 0;
    int x2 = 600;
    int y2 = 0;
    int x3 = 600;
    int y3 = 1920;
    int x4 = 300;
    int y4 = 1920;

    // thresh for detector
    float det_conf_thresh = 0.5f;

    // use GPU or CPU
    bool use_GPU = false;

    // it will trigger the warning monitoring
    int thresh_overlap_px = 100;

    // least count of trigger the warning
    int min_continousOverlapCount = 10;

    // the cycle of conducting caculation of detector
    int detect_cycle = 4;

    // number of threads to use up of the available CPU cores
    int num_threads = 16;
};

struct object_rect_ {
    int x;
    int y;
    int width;
    int height;
};

typedef struct HeadInfo
{
    std::string cls_layer;
    std::string dis_layer;
    int stride;
} HeadInfo;

struct CenterPrior
{
    int x;
    int y;
    int stride;
};

typedef struct BoxInfo
{
    float x1;
    float y1;
    float x2;
    float y2;
    float score;
    int label;
} BoxInfo;

class NanoDetVINO
{
public:
    NanoDetVINO(const char* param, AppConfig_* appConfig);

    ~NanoDetVINO();

    InferenceEngine::ExecutableNetwork network_;
    InferenceEngine::InferRequest infer_request_;
    // static bool hasGPU;

    // modify these parameters to the same with your config if you want to use your own model
    int input_size[2] = {416, 416}; // input height and width
    int num_class = 1; // number of classes. 80 for COCO
    int reg_max = 7; // `reg_max` set in the training config. Default: 7.
    std::vector<int> strides = { 8, 16, 32, 64 }; // strides of the multi-level feature.

    std::vector<BoxInfo> detect(cv::Mat image, float nms_threshold=0.5);

private:
    void preprocess(cv::Mat& image, InferenceEngine::Blob::Ptr& blob);
    void decode_infer(const float*& pred, std::vector<CenterPrior>& center_priors, float threshold, std::vector<std::vector<BoxInfo>>& results);
    BoxInfo disPred2Bbox(const float*& dfl_det, int label, float score, int x, int y, int stride);
    static void nms(std::vector<BoxInfo>& result, float nms_threshold);
    std::string input_name_ = "data";
    std::string output_name_ = "output";

    AppConfig_ mAppConfig;
};


#endif //_NANODE_TOPENVINO_H_
