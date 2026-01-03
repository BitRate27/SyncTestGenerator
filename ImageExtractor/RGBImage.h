#pragma once

#include <vector>
#include <cstdint>
#include <stdexcept>
#include "RGBPixel.h"

class RGBImage {
public:
	RGBImage();

	// Public so existing code can access the raw buffer without changes
	std::vector<uint8_t> data; // RGB triplets,8-bit per channel
	int width;
	int height;

	// Access pixel at (x, y) - Returns pointer to RGB triplet
	uint8_t *getPixel(int x, int y);
	const uint8_t *getPixel(int x, int y) const;

	// Return a cropped sub-image. Coordinates are [x0,y0) inclusive left/top and exclusive right/bottom: x in [x0,x1), y in [y0,y1).
	// Throws std::out_of_range for invalid rectangle.
	RGBImage crop(int x0, int y0, int x1, int y1) const;
};
