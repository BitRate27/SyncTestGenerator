#pragma once
#include <cstdint>
#include "RGBPixel.h"
#include "RGBImage.h"
#include <map>
#include <array>

class RGBHistogram {
public:
	RGBHistogram();
	~RGBHistogram();

	// Add a pixel value to the histogram
	void addPixel(RGB8Pixel value);

	// Get the count of a specific pixel value
	RGB8Pixel getPeak();

	// Clear the histogram
	void clear();

private:
	std::array<uint16_t,256> redCounts;
	std::array<uint16_t,256> greenCounts;
	std::array<uint16_t,256> blueCounts;
};
