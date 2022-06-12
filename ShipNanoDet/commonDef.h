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

	for (int i = 0; i < currentQueueLen - 1; i++)
	{
		auto delta = xArrays[i + 1] - xArrays[i];
		if (delta > 0)
			delta_positive++;
		else
			delta_negative++;
	}
	movingDirection = delta_negative >= delta_positive ? 0 : 1;
	return movingDirection;
}