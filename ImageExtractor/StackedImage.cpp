#include "StackedImage.h"
#include <stdexcept>
#include <cstdlib>
#include <cstring>

StackedImage::StackedImage() : width(0), height(0), layers(0), stride(0), pixelsize(0), ncolorvalues(0), ncolors(0), histograms(nullptr), nHistograms(0) {
	// initialize members
	filename.clear();
	img.width =0;
	img.height =0;
	img.data.clear();
}

StackedImage::~StackedImage()
{
	clear();
}

void StackedImage::clear()
{
	// No layerImages member in this version; just free histograms
	if (histograms) {
		std::free(histograms);
		histograms = nullptr;
		nHistograms =0;
	}

	// Clear stacked image
	img.data.clear();
	img.width =0;
	img.height =0;

	filename.clear();
	width =0;
	height =0;
	layers =0;
	stride =0;
	pixelsize =0;
	ncolorvalues =0;
	ncolors =0;
}

uint8_t linearInterpolate(uint16_t max, uint16_t t) {
	return static_cast<uint8_t>(((float)t/(float)max)*255.f);
}
RGB8Pixel StackedImage::getPeakPixel(int x, int y)
{
	if (!histograms || x <0 || x >= width || y <0 || y >= height) {
		return RGB8Pixel{0,0,0};
	}

	uint16_t maxRedCount = 0;
	uint16_t maxGreenCount = 0;
	uint16_t maxBlueCount = 0;
	uint8_t peakR = 0;
	uint8_t peakG = 0;
	uint8_t peakB = 0;

	size_t baseIndex =
		static_cast<size_t>(y) * static_cast<size_t>(stride) +
		static_cast<size_t>(x) * static_cast<size_t>(pixelsize);
	uint16_t *base = histograms + baseIndex;
	uint16_t *redCounts = base;
	uint16_t *greenCounts = base + ncolorvalues;
	uint16_t *blueCounts = greenCounts + ncolorvalues;
	if (stacked) {
		
		uint8_t red = (uint8_t)(std::min<uint16_t>(255,std::max<uint16_t>(0,redCounts[0]-88)));
		uint8_t green = (uint8_t)(std::min<uint16_t>(
			255, std::max<uint16_t>(0, greenCounts[0] - 133)));
		uint8_t blue = (uint8_t)(std::min<uint16_t>(
			255, std::max<uint16_t>(0, blueCounts[0] - 157)));
		return RGB8Pixel({red, green, blue});
	}
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
	if (maxRedCount > 10 || maxGreenCount > 10 || maxBlueCount > 10) {
		return RGB8Pixel{peakR, peakG, peakB};
	}
	return RGB8Pixel{0, 0, 0};
}

void StackedImage::addPixel(int x, int y, RGB8Pixel value) {
	if (!histograms) return;
	if (x <0 || x >= width || y <0 || y >= height) return;
	size_t baseIndex = static_cast<size_t>(y) * static_cast<size_t>(stride) + static_cast<size_t>(x) * static_cast<size_t>(pixelsize);
	uint16_t *base = histograms + baseIndex;
	uint16_t *redCounts = base;
	uint16_t *greenCounts = base + ncolorvalues;
	uint16_t *blueCounts = greenCounts + ncolorvalues;
	if (stacked) {
		redCounts[0] = std::min<uint16_t>(redCounts[0] + value.r, 65535);
		greenCounts[0] = std::min<uint16_t>(greenCounts[0] + value.g, 65535);
		blueCounts[0] = std::min<uint16_t>(blueCounts[0] + value.b, 65535);
		return;
	}
	redCounts[value.r]++;
	greenCounts[value.g]++;
	blueCounts[value.b]++;
}

bool StackedImage::addLayer(RGBImage &layerImg)
{
	if (width ==0) {
		// First layer with no offset: set base image size
		clear();
		width = layerImg.width;
		height = layerImg.height;
		// Allocate contiguous histogram buffer: uint16_t counts per color value per pixel
		int total = width * height;
		if (total ==0) return false;
		nHistograms = total;
		// configure histogram layout
		ncolorvalues = stacked ? 1 : 256;
		ncolors =3;
		pixelsize = ncolors * ncolorvalues; // number of uint16_t elements per pixel
		// total number of uint16_t elements
		size_t elementCount = static_cast<size_t>(total) * static_cast<size_t>(pixelsize);
		size_t byteSize = elementCount * sizeof(uint16_t);
		// allocate and zero
		histograms = static_cast<uint16_t*>(std::malloc(byteSize));
		if (!histograms) {
			nHistograms =0;
			return false;
		}
		std::memset(histograms,0, byteSize);
		stride = pixelsize * width; // number of uint16_t per row
	}

	try {
		for (int y =0; y < layerImg.height; ++y) {
			for (int x =0; x < layerImg.width; ++x) {
				const uint8_t *p = layerImg.getPixel(x, y);
				RGB8Pixel pix{p[0], p[1], p[2]};
				addPixel(x,y,pix);
			}
		}
		layers++;

	} catch (...) {
		return false;
	}

	return true;
}

RGBImage StackedImage::getPeakImage()
{
	RGBImage out;
	if (!histograms || width <=0 || height <=0) {
		// Return empty image
		out.width =0;
		out.height =0;
		out.data.clear();
		return out;
	}

	out.width = width;
	out.height = height;
	out.data.resize(static_cast<size_t>(width) * static_cast<size_t>(height) *3);

	for (int y =0; y < height; ++y) {
		for (int x =0; x < width; ++x) {
			RGB8Pixel p = getPeakPixel(x,y);
			size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) *3;
			out.data[idx +0] = p.r;
			out.data[idx +1] = p.g;
			out.data[idx +2] = p.b;
		}
	}

	return out;
}
