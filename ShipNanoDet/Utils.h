#pragma once
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

using namespace cv;

struct  AppConfig {
    // the input info, do not need to parse from json, just for external usage
    int input_width = 0;
    int input_height = 0;

    // the warning line points
    int x1 = 0;
    int y1 = 0;
    int x2 = 300;
    int y2 = 720;

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
} ;

struct object_rect {
    int x;
    int y;
    int width;
    int height;
};

