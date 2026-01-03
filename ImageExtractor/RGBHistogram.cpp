#include "RGBHistogram.h"

RGBHistogram::RGBHistogram() 
{
	redCounts.fill(0);
	greenCounts.fill(0);
	blueCounts.fill(0);
}

RGBHistogram::~RGBHistogram() {}

void RGBHistogram::addPixel(RGB8Pixel value)
{
    redCounts[value.r]++;
    greenCounts[value.g]++;
    blueCounts[value.b]++;
}

RGB8Pixel RGBHistogram::getPeak()
{
	uint16_t maxRedCount = 0;
	uint16_t maxGreenCount = 0;
	uint16_t maxBlueCount = 0;
	uint8_t peakR = 0;
	uint8_t peakG = 0;
	uint8_t peakB = 0;
	for (uint16_t i = 0; i < 256; ++i) {
		if (redCounts[i] > maxRedCount) {
			maxRedCount = redCounts[i];
			peakR = static_cast<uint8_t>(i);
		}
		if (greenCounts[i] > maxGreenCount) {
			maxGreenCount = greenCounts[i];
			peakG = static_cast<uint8_t>(i);
		}
		if (blueCounts[i] > maxBlueCount) {
			maxBlueCount = blueCounts[i];
			peakB = static_cast<uint8_t>(i);
		}
	}
	if (maxRedCount > 5 || maxGreenCount > 5 || maxBlueCount > 5) {
		return RGB8Pixel{peakR, peakG, peakB};
	}
	return RGB8Pixel{0,0,0};
}

void RGBHistogram::clear()
{
	redCounts.fill(0);
	greenCounts.fill(0);
	blueCounts.fill(0);
}
