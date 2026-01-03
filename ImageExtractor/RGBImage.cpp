#include "RGBImage.h"

RGBImage::RGBImage() : width(0), height(0) {}

uint8_t *RGBImage::getPixel(int x, int y)
{
	if (x <0 || x >= width || y <0 || y >= height) {
		throw std::out_of_range("Pixel coordinates out of bounds");
	}
	return &data[(y * width + x) *3];
}

const uint8_t *RGBImage::getPixel(int x, int y) const
{
	if (x <0 || x >= width || y <0 || y >= height) {
		throw std::out_of_range("Pixel coordinates out of bounds");
	}
	return &data[(y * width + x) *3];
}

RGBImage RGBImage::crop(int x0, int y0, int x1, int y1) const
{
	// Validate coordinates (x0,y0) inclusive, (x1,y1) exclusive
	if (x0 <0 || y0 <0 || x1 <= x0 || y1 <= y0 || x1 > width || y1 > height) {
		throw std::out_of_range("Invalid crop rectangle");
	}

	RGBImage out;
	out.width = x1 - x0;
	out.height = y1 - y0;
	out.data.resize(static_cast<size_t>(out.width) * static_cast<size_t>(out.height) *3);

	for (int y =0; y < out.height; ++y) {
		const uint8_t *srcRow = getPixel(x0, y0 + y);
		uint8_t *dstRow = &out.data[static_cast<size_t>(y) * out.width *3];
		for (int x =0; x < out.width; ++x) {
			dstRow[x *3 +0] = srcRow[x *3 +0];
			dstRow[x *3 +1] = srcRow[x *3 +1];
			dstRow[x *3 +2] = srcRow[x *3 +2];
		}
	}

	return out;
}

