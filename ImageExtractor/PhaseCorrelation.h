#pragma once

#include "RGBImage.h"
#include <vector>
#include <complex>
#include <cstddef>
#include <cstdint>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Structure to hold the shift result
struct ImageShift {
 double dx; // Shift in x direction (pixels, fractional)
 double dy; // Shift in y direction (pixels, fractional)
 double confidence; // Peak correlation value
};

class PhaseCorrelation {
public:
 // Calculate shift between two images using phase correlation
 static ImageShift calculateShift(const RGBImage &img1, const RGBImage &img2);

private:
 static std::vector<double> convertToGrayscale(const RGBImage &img);
 static void fft1D(std::vector<std::complex<double>> &data, bool inverse = false);
 static size_t nextPowerOf2(size_t n);
 static std::vector<std::complex<double>> computeFFT2D(const std::vector<double> &input, int width, int height);
 static std::vector<std::complex<double>> computeIFFT2D(const std::vector<std::complex<double>> &input, int width, int height);
 static ImageShift findPeak(const std::vector<std::complex<double>> &pcm, int width, int height);
};
