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
	int frameID;
	std::vector<ShipBox> boxes;
	bool isInDanger;
};
