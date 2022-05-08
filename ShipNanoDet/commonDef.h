#pragma once
#include <mat.h>
using namespace std;
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
	Json::Value detectedObjs;
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