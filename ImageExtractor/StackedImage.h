#pragma once

#include <vector>
#include <string>
#include "RGBImage.h"
#include "RGBPixel.h"

class StackedImage {
public:
	StackedImage();
	~StackedImage();

	RGBImage img;
	std::string filename;
	int width;
	int height;
	int layers;
	int stride;
	int pixelsize;
	int ncolorvalues;
	int ncolors;

	RGB8Pixel getPeakPixel(int x, int y);
	bool addLayer(RGBImage &layerImg);
	// Return an RGBImage composed of peak pixels from each histogram
	RGBImage getPeakImage();

private:
	void clear();
	void addPixel(int x, int y, RGB8Pixel value);
	uint16_t *histograms;
	int nHistograms;
};
