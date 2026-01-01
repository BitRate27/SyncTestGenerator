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
