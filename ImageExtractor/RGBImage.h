#pragma once

#include <vector>
#include <cstdint>
#include <stdexcept>

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
};
