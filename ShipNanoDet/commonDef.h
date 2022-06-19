#pragma once
#include <mat.h>
#include <thread>
#include "curl/curl.h"
#include <string>
#include <opencv2/opencv.hpp>
#include "tinyColormap.h"
#include "nanodet_openvino.h"

using namespace cv;
using namespace std;

#define TRACKING_COUNTS 10

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "wldap32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "wldap32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "Normaliz.lib")
struct ShipBox {
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
	float conf = 0.f;
	// indicate the ship is in left(0) /right(1) side of the dangerous region, -1 is invalid
	int regionLoc = -1;
};
struct FrameResult {
	/* cameraID used for identify where the stream comes from, need to set it inside the json config */
	int cameraID;
	int frameID;
	std::vector<ShipBox> boxes;
	bool isInDanger;
	/* expose the dangerous region indicating the configs from json file */
	int* dangerousRegion = new int[8];

	/* the timestamp for this current frame */
	string timeStamp;
};



string generateJsonResult(FrameResult frameResult)
{
	string jsonResult;

	Json::Value  root;
	
	// root: cameraID
	root["cameraID"] = frameResult.cameraID;

	// root: frameID
	root["frameID"] = frameResult.frameID;

	// root: isInDanger
	root["isIndanger"] = frameResult.isInDanger;

	// root: detectedObjs
	Json::Value detectedObjs = Json::arrayValue;
	// root->detectedObjs: jsonArray
	for (auto box : frameResult.boxes)
	{
		Json::Value shipBox;
		shipBox["x"] = box.x;
		shipBox["y"] = box.y;
		shipBox["width"] = box.width;
		shipBox["height"] = box.height;
		shipBox["conf"] = box.conf;
		shipBox["regionLoc"] = box.regionLoc;
		// append to the detectedObjs array
		detectedObjs.append(shipBox);
	}
	root["detectedObjs"] = detectedObjs;


	// root: dangerousRegion
	Json::Value dangerousRegion;
	for (auto i = 0; i < 8; i += 2)
	{
		Json::Value coordinate;
		coordinate["x"] = frameResult.dangerousRegion[i + 0];
		coordinate["y"] = frameResult.dangerousRegion[i + 1];
		// append to the dangerousRegion
		dangerousRegion.append(coordinate);
	}
	root["dangerousRegion"] = dangerousRegion;

	// timeStamp
	root["timeStamp"] = frameResult.timeStamp;

	jsonResult = root.toStyledString();

	return jsonResult;
}

CURLcode sendOutMetrics(AppConfig_ appConfig, const string jsonData)
{
	CURL* curl;
	CURLcode eRet = CURLcode::CURLE_OK;
	curl = curl_easy_init();
	if (curl) {
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
		curl_easy_setopt(curl, CURLOPT_URL, appConfig.remoteUrl.c_str());
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "https");
		curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, appConfig.timeout_for_sending_metrics_ms); // timeout is only 500ms by default
		struct curl_slist* headers = NULL;
		headers = curl_slist_append(headers, "Content-Type: application/json");
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		const char* data = jsonData.c_str();
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
		eRet = curl_easy_perform(curl);
	}
	curl_easy_cleanup(curl);
	return eRet;
}




Scalar getTrackerColor(int trackerID) {
	
	float value = 1.f * (trackerID % TRACKING_COUNTS) / TRACKING_COUNTS;
	const tinycolormap::Color color = tinycolormap::GetQuantizedColor(value, TRACKING_COUNTS, tinycolormap::ColormapType::Parula);
	// construct the color for different detected object
	return Scalar(color.ri(), color.gi(), color.bi());
}




bool sendOutMetrics_Simulation(const char* url, const string jsonData)
{
	printf("simulation on the sendingOut metrics started\n");
	printf("processing...\n %s \n", jsonData.c_str());
	Sleep(200);
	printf("simulation on the sendingOut metrics finished\n");
	return true;
}

/*
*  0 stands for right->left, 1 stands for left->right
*/
int calculateDirection(std::vector<BoxInfo> historyBoxLocations, int minLengthToDecide) {
	int movingDirection = -1;
	int currentQueueLen = historyBoxLocations.size();
	if (currentQueueLen < minLengthToDecide) return movingDirection;
	int* xArrays = new int[currentQueueLen];

	for (int i = 0; i < currentQueueLen; i++)
	{
		auto centerX = (int)((historyBoxLocations[i].x1 + historyBoxLocations[i].x2) / 2);
		xArrays[i] = centerX;
	}

	int delta_positive = 0, delta_negative = 0;
	int lastElement = xArrays[currentQueueLen - 1];
	for (int i = 0; i < currentQueueLen - 2; i++)
	{
		auto delta = lastElement - xArrays[i];
		if (delta > 0)
			delta_positive++;
		else
			delta_negative++;
	}
	movingDirection = delta_negative >= delta_positive ? 0 : 1;
	return movingDirection;
}


void drawPatch(const Mat& srcImage_, Mat& frame, const cv::Point location) {
	Mat srcImage = srcImage_.clone();
	if (!srcImage.data || !frame.data) return;
	Mat mask;
	vector<Mat> rgbLayer;
	split(srcImage, rgbLayer);
	if (srcImage.channels() == 4)
	{
		
		split(srcImage, rgbLayer);
		Mat cs[3] = {rgbLayer[0], rgbLayer[1], rgbLayer[2]};
		merge(cs, 3, srcImage);
		mask = rgbLayer[3]; // alpha channel as mask
	}
	cv::Rect roi(location.x, location.y, srcImage.cols, srcImage.rows);
	if (roi.x >= 0 && roi.y >= 0 && roi.x + roi.width < frame.cols && roi.y + roi.height < frame.rows)
		srcImage.copyTo(frame(roi), mask);
	else
	{
		printf("error: the roi is illegal to draw this patch\n");
	}
	srcImage.release();
}


int calculateShipRegionLoc(AppConfig_* appConfig, BoxInfo boxInfo) {
	// x = x1 + (y - y1) * delta ; delta = ((x2 - x1) / (y2 - y1))
	float delta_right = float(appConfig->x2 - appConfig->x3) / float(appConfig->y2 - appConfig->y3);
	float delta_left = float(appConfig->x1 - appConfig->x4) / float(appConfig->y1 - appConfig->y4);

	// x = x2 + (y - y2) * delta ; delta = ((x2 - x1) / (y2 - y1))
	auto tmp_x_right_up = appConfig->x2 + (boxInfo.y1 - appConfig->y2) * delta_right;
	auto tmp_x_right_down = appConfig->x2 + (boxInfo.y2 - appConfig->y2) * delta_right;
	auto tmp_x_left_up = appConfig->x1 + (boxInfo.y1 - appConfig->y1) * delta_left;
	auto tmp_x_left_down = appConfig->x1 + (boxInfo.y2 - appConfig->y1) * delta_left;

	// newly constructed rectangle
	auto dangerousRegion_x1 = min(tmp_x_left_up, tmp_x_left_down);
	auto dangerousRegion_y1 = boxInfo.y1;
	auto dangerousRegion_x2 = max(tmp_x_right_up, tmp_x_right_down);
	auto dangerousRegion_y2 = boxInfo.y2;

	// ship is right of the dangerous region
	if (boxInfo.x1 >= dangerousRegion_x2)
		return 1; // indicating it is in right region loc
	if (boxInfo.x2 <= dangerousRegion_x1)
		return 0; // indicating it is in left  region loc

	return -1; // invalid
}