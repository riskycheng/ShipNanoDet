#include "nanodet_openvino.h"
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <iostream>
#include <limits>
#include "json.h"
#include <fstream>
#include "commonDef.h"
#include "vlc_reader.h"

#define CAM_URL "http://shanghai.wangshiyao.com:8005/Info/cameraInfo"
using namespace Json;
using namespace std;
using namespace cv;

static AppConfig_ mAppConfig;
void DrawPolygon(Mat inputImage, vector<Point> polygonPoint, bool bIsFill, bool bIsClosed = true);

void printFrameResult_(FrameResult result) {
    if (!mAppConfig.enable_debugging_log) return;
    printf("******************** FrameResult *********************\n");
    printf("frameID:%d\n", result.frameID);
    printf("In Danger:%s\n", result.isInDanger ? "true" : "false");
    printf("Detected Ships:\n");
    for (auto& ship : result.boxes)
    {
        printf("\t[x: %d, y: %d, width: %d, height: %d, conf: %.2f] \n", ship.x, ship.y, ship.width, ship.height, ship.conf);
    }
    printf("******************** FrameResult *********************\n");
}
BoxInfo mapCoordinates(AppConfig_* appConfig, BoxInfo box, object_rect_ effect_roi) {
    BoxInfo result = BoxInfo();

    int src_w = appConfig->input_width;
    int src_h = appConfig->input_height;
    int dst_w = effect_roi.width;
    int dst_h = effect_roi.height;
    float width_ratio = (float)src_w / (float)dst_w;
    float height_ratio = (float)src_h / (float)dst_h;

    result.x1 = (box.x1 - effect_roi.x) * width_ratio;
    result.y1 = (box.y1 - effect_roi.y) * height_ratio;
    result.x2 = (box.x2 - effect_roi.x) * width_ratio;
    result.y2 = (box.y2 - effect_roi.y) * height_ratio;

    result.label = box.label;
    result.score = box.score;

    return result;
}



void DrawPolygon(Mat inputImage, vector<Point> polygonPoint, bool bIsFill, bool bIsClosed)
{
    vector<vector<Point>> contours;
    contours.push_back(polygonPoint);

    if (bIsFill)
        fillPoly(inputImage, contours, Scalar(0, 0, 255), 8);
    else
        polylines(inputImage, polygonPoint, bIsClosed, Scalar(0, 0, 255), 2, 8);
}


// the global static value indicating the hit count
static int dangerousHitCnt = 0;

bool touchWarningLines(AppConfig_* appConfig, vector<BoxInfo>& boxes, object_rect_ effect_roi);
bool touchWarningLinesPologyn(AppConfig_* appConfig, vector<BoxInfo>& boxes, object_rect_ effect_roi);

bool touchWarningLinesPologyn(AppConfig_* appConfig, vector<BoxInfo>& boxes, object_rect_ effect_roi)
{
    // x = x1 + (y - y1) * delta ; delta = ((x2 - x1) / (y2 - y1))
    float delta_right = float(appConfig->x2 - appConfig->x3) / float(appConfig->y2 - appConfig->y3);
    float delta_left = float(appConfig->x1 - appConfig->x4) / float(appConfig->y1 - appConfig->y4);
    
    int centerX = (appConfig->x1 + appConfig->x3) / 2;
    bool isInLeft = (centerX <= appConfig->input_width / 2);

    int targetIndex = 0;
    int minX = 10000;
    int maxX = 0;
    for (int i = 0; i < boxes.size(); i++)
    {
        auto item = mapCoordinates(appConfig, boxes[i], effect_roi);
        if (isInLeft)
        {
            if (item.x1 < minX)
            {
                minX = item.x1;
                targetIndex = i;
            }
        }
        else
        {
            if (item.x2 > maxX)
            {
                maxX = item.x2;
                targetIndex = i;
            }
        }
    }

    bool touched = false;

    int thresh_dis = appConfig->thresh_overlap_px;
    int thresh_cnt = appConfig->min_continousOverlapCount;
    bool tmpTouched = false;

    // start cal the distance
    auto boxInfo = mapCoordinates(appConfig, boxes[targetIndex], effect_roi);
        
    // x = x2 + (y - y2) * delta ; delta = ((x2 - x1) / (y2 - y1))
    auto tmp_x_right_up   = appConfig->x2 + (boxInfo.y1 - appConfig->y2) * delta_right;
    auto tmp_x_right_down = appConfig->x2 + (boxInfo.y2 - appConfig->y2) * delta_right;
    auto tmp_x_left_up    = appConfig->x1 + (boxInfo.y1 - appConfig->y1) * delta_left;
    auto tmp_x_left_down  = appConfig->x1 + (boxInfo.y2 - appConfig->y1) * delta_left;
   
    // newly constructed rectangle
    auto dangerousRegion_x1 = min(tmp_x_left_up, tmp_x_left_down);
    auto dangerousRegion_y1 = boxInfo.y1;
    auto dangerousRegion_x2 = max(tmp_x_right_up, tmp_x_right_down);
    auto dangerousRegion_y2 = boxInfo.y2;

    // start calculating the touching status >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
    if (boxInfo.x2 < dangerousRegion_x1 || boxInfo.x1 > dangerousRegion_x2)
    {
        // it is outside the region
        tmpTouched = false;
    }
    else if (boxInfo.x2 > dangerousRegion_x1 && boxInfo.x2 < dangerousRegion_x2)
    {
        if ((boxInfo.x2 - dangerousRegion_x1) >= thresh_dis)
        {
            // partial inside: right
            tmpTouched = true;
        }
    }
    else if (boxInfo.x1 > dangerousRegion_x1 && boxInfo.x1 < dangerousRegion_x2)
    {
        if ((dangerousRegion_x2 - boxInfo.x1) >= thresh_dis)
        {
            // partial inside: left
            tmpTouched = true;
        }
    }
    else if (boxInfo.x1 < dangerousRegion_x1 && boxInfo.x2 > dangerousRegion_x2)
    {
        // box includes dangerous region
        tmpTouched = true;
    }
    else if (boxInfo.x1 > dangerousRegion_x1 && boxInfo.x2 < dangerousRegion_x2)
    {
        // dangerous region includes box
        tmpTouched = true;
    }

    if (tmpTouched)
    {
        dangerousHitCnt++;
    }
    else
    {
        dangerousHitCnt = 0;
    }

    if (tmpTouched && dangerousHitCnt >= thresh_cnt)
    {
        touched = true;
    }
    else
    {
        touched = false;
    }
    return touched;
}



bool touchWarningLines(AppConfig_* appConfig, vector<BoxInfo>& boxes, object_rect_ effect_roi) {

    bool touched = false;

    int thresh_dis = appConfig->thresh_overlap_px;
    int thresh_cnt = appConfig->min_continousOverlapCount;
    bool tmpTouched = false;

    // pick the closest item to the dangerous region
    // there is one assumption here:
    //   - if the dangerous region is on left, ships going from right to left
    //   - if the dangerous region is on right,ships going from left to right
    int centerX = (appConfig->x1 + appConfig->x2) / 2;
    bool isInLeft = (centerX <= appConfig->input_width / 2);

    int targetIndex = 0;
    int minX = 10000;
    int maxX = 0;
    for (int i = 0; i < boxes.size(); i++)
    {
        auto item = mapCoordinates(appConfig, boxes[i], effect_roi);
        if (isInLeft)
        {
            if (item.x1 < minX)
            {
                minX = item.x1;
                targetIndex = i;
            }
        }
        else
        {
            if (item.x2 > maxX)
            {
                maxX = item.x2;
                targetIndex = i;
            }
        }
    }

    auto boxInfo = mapCoordinates(appConfig, boxes[targetIndex], effect_roi);

    if (boxInfo.x2 < appConfig->x1 || boxInfo.x1 > appConfig->x2)
    {
        // it is outside the region
        tmpTouched = false;
    }
    else if (boxInfo.x2 > appConfig->x1 && boxInfo.x2 < appConfig->x2)
    {
        if ((boxInfo.x2 - appConfig->x1) >= thresh_dis)
        {
            // partial inside: right
            tmpTouched = true;
        }
    }
    else if (boxInfo.x1 > appConfig->x1 && boxInfo.x1 < appConfig->x2)
    {
        if ((appConfig->x2 - boxInfo.x1) >= thresh_dis)
        {
            // partial inside: left
            tmpTouched = true;
        }
    }
    else if (boxInfo.x1 < appConfig->x1 && boxInfo.x2 > appConfig->x2)
    {
        // box includes dangerous region
        tmpTouched = true;
    }
    else if (boxInfo.x1 > appConfig->x1 && boxInfo.x2 < appConfig->x2)
    {
        // dangerous region includes box
        tmpTouched = true;
    }

    if (tmpTouched)
    {
        dangerousHitCnt++;
    }
    else
    {
        dangerousHitCnt = 0;
    }

    if (tmpTouched && dangerousHitCnt >= thresh_cnt)
    {
        touched = true;
    }
    else
    {
        touched = false;
    }
    return touched;
}

void parseConfig(const std::string jsonConfigPath) {
    std::ifstream file_input(jsonConfigPath.c_str());
    Json::Reader reader;
    Json::Value root;
    reader.parse(file_input, root);

    // detector related
    float det_conf_thresh = root["detector"]["det_conf_thresh"].asFloat();
    bool use_GPU = root["detector"]["use_GPU"].asBool();

    // application related
    // camera related
    int cameraID = root["application"]["camera_related"]["cameraID"].asInt();
    int sourceMode = root["application"]["camera_related"]["sourceMode"].asInt();
    static string sourceLocation = root["application"]["camera_related"]["sourceLocation"].asString();

    int x1 = root["application"]["dangerous_region"]["x1"].asInt();
    int y1 = root["application"]["dangerous_region"]["y1"].asInt();
    int x2 = root["application"]["dangerous_region"]["x2"].asInt();
    int y2 = root["application"]["dangerous_region"]["y2"].asInt();
    int x3 = root["application"]["dangerous_region"]["x3"].asInt();
    int y3 = root["application"]["dangerous_region"]["y3"].asInt();
    int x4 = root["application"]["dangerous_region"]["x4"].asInt();
    int y4 = root["application"]["dangerous_region"]["y4"].asInt();

    int thresh_overlap_px = root["application"]["thresh_overlap_px"].asInt();

    int min_continousOverlapCount = root["application"]["min_continousOverlapCount"].asInt();

    int detect_cycle = root["application"]["detect_cycle"].asInt();

    int num_threads = root["application"]["num_threads"].asInt();

    static string remoteURL = root["application"]["remote_url"].asString();

    int local_metrics_sending_queue_length = root["application"]["local_metrics_sending_queue_length"].asInt();

    bool enable_debugging_log = root["application"]["enable_debugging_log"].asBool();

    int timeout_for_sending_metrics_ms = root["application"]["timeout_for_sending_metrics_ms"].asInt();

    // start updating for these fields
    mAppConfig.det_conf_thresh = det_conf_thresh;
    mAppConfig.x1 = x1;
    mAppConfig.y1 = y1;
    mAppConfig.x2 = x2;
    mAppConfig.y2 = y2;
    mAppConfig.x3 = x3;
    mAppConfig.y3 = y3;
    mAppConfig.x4 = x4;
    mAppConfig.y4 = y4;

    mAppConfig.thresh_overlap_px = thresh_overlap_px;

    mAppConfig.min_continousOverlapCount = min_continousOverlapCount;

    mAppConfig.detect_cycle = detect_cycle;

    mAppConfig.num_threads = num_threads;

    // update the camera related fields
    mAppConfig.cameraID = cameraID;
    mAppConfig.sourceMode = sourceMode;
    mAppConfig.sourceLocation = sourceLocation;

    mAppConfig.remoteUrl = remoteURL;

    mAppConfig.enable_debugging_log = enable_debugging_log;

    mAppConfig.local_metrics_sending_queue_length = local_metrics_sending_queue_length;

    mAppConfig.timeout_for_sending_metrics_ms = timeout_for_sending_metrics_ms;
}


void printAppConfig(const AppConfig_& appConfig) {
    printf("******************** AppConfig *********************\n");
    printf("Detector related:\n");
    printf("\t det_conf_thresh:%.2f\n", appConfig.det_conf_thresh);
    printf("\t use_GPU:%s\n", appConfig.use_GPU ? "true" : "false");
    printf("Application related:\n");
    printf("\t warning_line: [%d, %d] -> [%d, %d] -> [%d, %d] -> [%d, %d]\n", 
        appConfig.x1, appConfig.y1, appConfig.x2, appConfig.y2,
        appConfig.x3, appConfig.y3, appConfig.x4, appConfig.y4);
    printf("\t thresh_overlap_px:%d\n", appConfig.thresh_overlap_px);
    printf("\t min_continousOverlapCount:%d\n", appConfig.min_continousOverlapCount);
    printf("\t detect_cycle:%d\n", appConfig.detect_cycle);
    printf("\t num_threads:%d\n", appConfig.num_threads);
    printf("\t remoteUrl:%s\n", appConfig.remoteUrl.c_str());
    printf("\t local_metrics_sending_queue_length:%d\n", appConfig.local_metrics_sending_queue_length);
    printf("\t enable_debugging_log:%s\n", appConfig.enable_debugging_log ? "true" : "false");
    printf("\t timeout_for_sending_metrics_ms:%d\n", appConfig.timeout_for_sending_metrics_ms);
    printf("******************** AppConfig *********************\n");
}

int resize_uniform_VINO(cv::Mat& src, cv::Mat& dst, cv::Size dst_size, object_rect_& effect_area)
{
    int w = src.cols;
    int h = src.rows;
    int dst_w = dst_size.width;
    int dst_h = dst_size.height;
    // std::cout << "src: (" << h << ", " << w << ")" << std::endl;
    dst = cv::Mat(cv::Size(dst_w, dst_h), src.channels() == 3 ? CV_8UC3 : CV_8UC4, cv::Scalar(0));

    float ratio_src = w * 1.0 / h;
    float ratio_dst = dst_w * 1.0 / dst_h;

    int tmp_w = 0;
    int tmp_h = 0;
    if (ratio_src > ratio_dst) {
        tmp_w = dst_w;
        tmp_h = floor((dst_w * 1.0 / w) * h);
    }
    else if (ratio_src < ratio_dst) {
        tmp_h = dst_h;
        tmp_w = floor((dst_h * 1.0 / h) * w);
    }
    else {
        cv::resize(src, dst, dst_size);
        effect_area.x = 0;
        effect_area.y = 0;
        effect_area.width = dst_w;
        effect_area.height = dst_h;
        return 0;
    }

    //std::cout << "tmp: (" << tmp_h << ", " << tmp_w << ")" << std::endl;
    cv::Mat tmp;
    cv::resize(src, tmp, cv::Size(tmp_w, tmp_h));

    if (tmp_w != dst_w) {
        int index_w = floor((dst_w - tmp_w) / 2.0);
        //std::cout << "index_w: " << index_w << std::endl;
        for (int i = 0; i < dst_h; i++) {
            memcpy(dst.data + i * dst_w * 3 + index_w * 3, tmp.data + i * tmp_w * 3, tmp_w * 3);
        }
        effect_area.x = index_w;
        effect_area.y = 0;
        effect_area.width = tmp_w;
        effect_area.height = tmp_h;
    }
    else if (tmp_h != dst_h) {
        int index_h = floor((dst_h - tmp_h) / 2.0);
        //std::cout << "index_h: " << index_h << std::endl;
        memcpy(dst.data + index_h * dst_w * 3, tmp.data, tmp_w * tmp_h * 3);
        effect_area.x = 0;
        effect_area.y = index_h;
        effect_area.width = tmp_w;
        effect_area.height = tmp_h;
    }
    else {
        printf("error in resize_uniform_VINO\n");
    }
    tmp.release();
    return 0;
}

const int color_list[80][3] =
{
    {216 , 82 , 24}
};

void draw_bboxes(const cv::Mat& bgr, const std::vector<BoxInfo>& bboxes, object_rect_ effect_roi, AppConfig_* appConfig, bool warning)
{
    static const char* class_names[] = { "ship" };

    cv::Mat image = bgr.clone();
    int src_w = image.cols;
    int src_h = image.rows;
    int dst_w = effect_roi.width;
    int dst_h = effect_roi.height;
    float width_ratio = (float)src_w / (float)dst_w;
    float height_ratio = (float)src_h / (float)dst_h;



    for (size_t i = 0; i < bboxes.size(); i++)
    {
        const BoxInfo& bbox = bboxes[i];
        //fprintf(stderr, "%d = %.5f at %.2f %.2f %.2f %.2f\n", bbox.label, bbox.score,
        //    bbox.x1, bbox.y1, bbox.x2, bbox.y2);

        cv::rectangle(image, cv::Rect(cv::Point((bbox.x1 - effect_roi.x) * width_ratio, (bbox.y1 - effect_roi.y) * height_ratio),
            cv::Point((bbox.x2 - effect_roi.x) * width_ratio, (bbox.y2 - effect_roi.y) * height_ratio)), cv::Scalar(0, 250, 0), 2);

        char* text = new char[50];
        sprintf(text, "%s %.1f%%", class_names[bbox.label], bbox.score * 100);

        int baseLine = 0;
        cv::Size label_size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 0.8, 1, &baseLine);

        int x = (bbox.x1 - effect_roi.x) * width_ratio;
        int y = (bbox.y1 - effect_roi.y) * height_ratio - label_size.height - baseLine;
        if (y < 0)
            y = 0;
        if (x + label_size.width > image.cols)
            x = image.cols - label_size.width;

        cv::rectangle(image, cv::Rect(cv::Point(x, y), cv::Size(label_size.width, label_size.height + baseLine)),
            cv::Scalar(0, 250, 0), -1);

        cv::putText(image, text, cv::Point(x, y + label_size.height),
            cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(255, 255, 255));

        delete[] text;
    }

    // render the dangerous region
    //cv::rectangle(image, cv::Rect(cv::Point(appConfig->x1, appConfig->y1), cv::Point(appConfig->x2, appConfig->y2)), cv::Scalar(0, 0, 255), 2);
    vector<Point> points;
    points.push_back(cv::Point(appConfig->x1, appConfig->y1));
    points.push_back(cv::Point(appConfig->x2, appConfig->y2));
    points.push_back(cv::Point(appConfig->x3, appConfig->y3));
    points.push_back(cv::Point(appConfig->x4, appConfig->y4));
    DrawPolygon(image, points, false);
    
    // render the text hint to indicate it is dangerous region
    string dangerousRegionName = "dangerous region";
    auto baseLine = 0;
    auto label_size = cv::getTextSize(dangerousRegionName, cv::FONT_HERSHEY_PLAIN, 0.6f, 1, &baseLine);
    cv::putText(image, dangerousRegionName.c_str(), cv::Point(appConfig->x3 - label_size.width / 0.5f, appConfig->y3 - label_size.height),
        cv::FONT_HERSHEY_SIMPLEX, 0.6f, cv::Scalar(255, 255, 255));

    // render the dangerous hint if hit
    int warningBoxWidth = 300;
    int warningBoxHeight = 50;
    int warningBoxTop = 150;
    auto warningTxt = "Danger Detected";
    int warningBox_x1 = (appConfig->input_width - warningBoxWidth) / 2;
    int warningBox_y1 = warningBoxTop;
    int warningBox_x2 = (appConfig->input_width + warningBoxWidth) / 2;
    int warningBox_y2 = warningBox_y1 + warningBoxHeight;
    auto warningBoxRect = cv::Rect(cv::Point(warningBox_x1, warningBox_y1), cv::Point(warningBox_x2, warningBox_y2));

    if (warning)
    {
        // render the out-lier
        cv::rectangle(image, warningBoxRect, cv::Scalar(50, 120, 250), 6);
        // fill in
        cv::rectangle(image, warningBoxRect, cv::Scalar(50, 200, 250), -1);
        // render the text
        int baseLine = 0;
        cv::Size label_size = cv::getTextSize(warningTxt, cv::FONT_HERSHEY_PLAIN, 1.0f, 1, &baseLine);
        int text_x = warningBox_x1 + (warningBoxWidth - label_size.width) / 8;
        int text_y = (warningBox_y1 + warningBox_y2) / 2 + baseLine;
        cv::putText(image, warningTxt, cv::Point(text_x, text_y),
            cv::FONT_HERSHEY_SIMPLEX, 1.0f, cv::Scalar(255, 255, 255));
    }
    resize(image, image, cv::Size(image.cols / 2, image.rows / 2), cv::INTER_NEAREST);
    cv::imshow("shipDet v1.7_20220528_openVINO", image);
    cv::waitKey(1);
    image.release();
}

FrameResult imageRun(int frameID, NanoDetVINO& detector, Mat& image, AppConfig_* appConfig, bool skip) {
    int height = detector.input_size[0];
    int width = detector.input_size[1];

    // buffered results
    std::vector<BoxInfo> results;

    FrameResult frameResult = FrameResult();

    // get the current system time stamp
    time_t timep;
    struct tm* p;
    time(&timep);
    p = localtime(&timep);
    //2021/5/11 14:04:44
    char* timeStamp = new char[50];
    sprintf(timeStamp, "%d/%d/%d %02d:%02d:%02d", 1900 + p->tm_year, 1 + p->tm_mon,
        p->tm_mday, p->tm_hour, p->tm_min, p->tm_sec);
    // assign it back to the frameResult struct
    frameResult.timeStamp = timeStamp;
    delete[] timeStamp;

    // update the cameraID
    frameResult.cameraID = appConfig->cameraID;

    // update the dangerous region
    frameResult.dangerousRegion[0] = appConfig->x1;
    frameResult.dangerousRegion[1] = appConfig->y1;
    frameResult.dangerousRegion[2] = appConfig->x2;
    frameResult.dangerousRegion[3] = appConfig->y2;
    frameResult.dangerousRegion[4] = appConfig->x3;
    frameResult.dangerousRegion[5] = appConfig->y3;
    frameResult.dangerousRegion[6] = appConfig->x4;
    frameResult.dangerousRegion[7] = appConfig->y4;

    clock_t start, end;
    double cost;
    int frameIndex = 0;
    int cycle = appConfig->detect_cycle;
    object_rect_ effect_roi{};
    bool touchedWarning = false;
    if (skip)
    {
        if (mAppConfig.enable_debugging_log)
            // not inference, use the previous one
            printf("use the previous inferred result...\n");
        touchedWarning = touchWarningLinesPologyn(appConfig, results, effect_roi);
        draw_bboxes(image, results, effect_roi, appConfig, touchedWarning);
        cv::waitKey(30);
    }
    else
    {
        int img_width = image.cols;
        int img_height = image.rows;
        appConfig->input_height = img_height;
        appConfig->input_width = img_width;
        // do the inference
        cv::Mat resized_img;
        start = clock();

        resize_uniform_VINO(image, resized_img, cv::Size(width, height), effect_roi);

        //char* imageName = new char[200];
        //sprintf(imageName, "./tmp_images/%04d.jpg", frameID);
        //imwrite(imageName, resized_img);
        //delete[] imageName;

        results = detector.detect(resized_img);
        end = clock();
        cost = difftime(end, start);
        if (!results.empty())
            touchedWarning = touchWarningLinesPologyn(appConfig, results, effect_roi);
        if (mAppConfig.enable_debugging_log)
            printf("inference video with OpenVINO cost: %.2f ms \n", cost);
        draw_bboxes(image, results, effect_roi, appConfig, touchedWarning);
        cv::waitKey(1);
        resized_img.release();
    }
    // update fields of FrameResult
    frameResult.frameID = frameID;
    for (BoxInfo& box_ : results)
    {
        auto box = mapCoordinates(appConfig, box_, effect_roi);
        auto shipBox = ShipBox();
        shipBox.x = box.x1;
        shipBox.y = box.y1;
        shipBox.width = box.x2 - box.x1;
        shipBox.height = box.y2 = box.y1;
        shipBox.conf = box.score;
        frameResult.boxes.push_back(shipBox);
    }
    frameResult.isInDanger = touchedWarning;

    return frameResult;
}

vector<FrameResult> mFrameResutsCached;
bool mSendOutThreadRun = true;
void sendOutMetricsThread() {
    
    while (mSendOutThreadRun)
    {
        // if the cached queue is too long, need to clear it up to avoid latency
        if (mFrameResutsCached.size() > mAppConfig.local_metrics_sending_queue_length)
        {
            mFrameResutsCached.clear();
            if (mAppConfig.enable_debugging_log)
                printf("current local metrics queue cleared up to avoid lantency > %d \n", mAppConfig.local_metrics_sending_queue_length);
        }
        

        // check the queue every 5ms
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (!mFrameResutsCached.empty())
        {
            auto item = mFrameResutsCached[0];
            // print out the json metrics
            string jsonStr = generateJsonResult(item);
            //sendOutMetrics(REMOTE_SERVICE_ADDR.c_str(), jsonStr);
            if (mAppConfig.enable_debugging_log)
                printf("start sending out frameResult...\n");


            auto eRet = sendOutMetrics(mAppConfig, jsonStr);
  
            if (eRet == CURLE_FAILED_INIT)
            {
                printf("[error] failed to init the Curl interface...\n");
            }
            else if (eRet == CURLE_COULDNT_RESOLVE_PROXY || eRet == CURLE_COULDNT_RESOLVE_HOST)
            {
                printf("[error] cannot resolve the host, ensure the host address is correct...\n");
            }
            else if (eRet == CURLE_COULDNT_CONNECT)
            {
                printf("[error] cannot connect to remote serive, ensure it started properly...\n");
            }
            else if (eRet == CURLE_OK)
            {
                if (mAppConfig.enable_debugging_log)
                {
                    printf("[success] remote API calling succeeds...\n");
                } 
            }
            else if (eRet == CURLE_OPERATION_TIMEDOUT)
            {
                printf("[error] timeOut sending out metrics to %s...\n", mAppConfig.remoteUrl.c_str());
            }
            else
            {
                printf("[error] failed to send out metrics due to %s...\n", eRet);
            }
            // remove the first one 
            mFrameResutsCached.erase(mFrameResutsCached.begin());
        }
        else {
            if (mAppConfig.enable_debugging_log)
                printf("no valid result in the queue pool yet \n");
        }
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        printf("usage: shipDet.exe [config_file_path] \n e.g., \n shipDet.exe config.json");
        return -1;
    }
    std::cout << "start init model with openVINO..." << std::endl;


    // parse the json for initialization
    std::string jsonFilePath = argv[1];
    parseConfig(jsonFilePath);
    printAppConfig(mAppConfig);

    // parse the video file path
    std::string videoFilePath = mAppConfig.sourceLocation;

    // model path
    bool modelInit = false;
    auto detector = NanoDetVINO("./models/openVINOModel/nanodet.xml", &mAppConfig, modelInit);
    
    if (modelInit)
    {
        printf("openVINO model initialized successfully! \n");
    }
    else
    {
        printf("openVINO model initialized failed, exiting... \n");
        return -1;
    }


    // the offline video mode
    cv::VideoCapture videoCap;

    // the VLC for remote streaming mode
    char* address = new char[200];
    sprintf(address, "%s", videoFilePath.c_str());
    int rtsp_w = 1920, rtsp_h = 1080;
    vlc_reader vlcReader(address);


    switch (mAppConfig.sourceMode)
    {
    case 0: // the offline video mode
    case 1: // the live local camera mode
        videoCap.open(videoFilePath.c_str());
        break;
    case 2: // the remote RTSP stream
        if (mAppConfig.enable_debugging_log)
            printf("loading rtsp from:%s \n", mAppConfig.sourceLocation.c_str());
        vlcReader.start(rtsp_w, rtsp_h);
        break;
    case 3: // the still image mode, it is mainly used for debugging
        if (mAppConfig.enable_debugging_log)
            printf("loading image from:%s \n", mAppConfig.sourceLocation.c_str());
        break;
    default:
        break;
    }

    thread rst = thread(sendOutMetricsThread);

    int cycle = mAppConfig.detect_cycle;
    FrameResult frameResult;
    cv::Mat image;
    int frameIndex = 0;
    while (true)
    {
        switch (mAppConfig.sourceMode)
        {
        case 0: // the offline video mode
        case 1: // the live local camera mode
            videoCap.read(image);
            break;

        case 2: // the remote RTSP stream
            image = vlcReader.frame().clone();
            if (image.channels() == 4)
                cvtColor(image, image, COLOR_RGBA2RGB);
            else
            {
                printf("the remote rtsp frame not ready...\n");
                continue;
            }

            break;
        case 3:
            image = imread(mAppConfig.sourceLocation.c_str());
        default:
            break;
        }

        if (image.data == nullptr)
        {
            printf("no valid frame, wait for next... \n");
            continue;
        }

        if (frameIndex % cycle != 0)
        {
            // not inference, use the previous one
            frameResult = imageRun(frameIndex, detector, image, &mAppConfig, true);
            cv::waitKey(30);
        }
        else
        {
            // do the inference
            frameResult = imageRun(frameIndex, detector, image, &mAppConfig, false);
        }

        frameIndex++;
        // print out metrics
        printFrameResult_(frameResult);

        // sendOutMetrics(appConfig.remoteUrl.c_str(), jsonStr);
        //std::thread threadAction(sendOutMetrics, appConfig.remoteUrl.c_str(), jsonStr);
        // keep adding to the global queue
        mFrameResutsCached.emplace_back(frameResult);
    }

    if (videoCap.isOpened())
    {
        videoCap.release();
    }

    if (mAppConfig.sourceMode == 3)
    {
        // indicate it is image mode
        waitKey();
    }

    image.release();
    return 0;
}