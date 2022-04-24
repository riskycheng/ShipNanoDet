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
