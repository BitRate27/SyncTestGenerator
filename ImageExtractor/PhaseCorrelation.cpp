#include "PhaseCorrelation.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Helper: parabolic interpolation to refine peak location
static double parabolicInterp(double xm1, double x0, double xp1)
{
    // fit a parabola through (-1,xm1), (0,x0), (1,xp1)
    double denom = (xm1 - 2.0 * x0 + xp1);
    if (std::abs(denom) < 1e-12) return 0.0; // no curvature
    return 0.5 * (xm1 - xp1) / denom; // offset from center
}

ImageShift PhaseCorrelation::calculateShift(const RGBImage &img1, const RGBImage &img2)
{
    if (img1.width != img2.width || img1.height != img2.height) {
        throw std::invalid_argument("Images must have the same dimensions");
    }

    int width = img1.width;
    int height = img1.height;

    // Use only the specified square region for shift calculation
    const int x0 = 2600;
    const int y0 = 2450;
    const int x1 = 2900; // exclusive end = x0 +300
    const int y1 = 2750; // exclusive end = y0 +300

    if (x0 < 0 || y0 < 0 || x1 > width || y1 > height || x1 <= x0 || y1 <= y0) {
        throw std::invalid_argument("Requested region is out of image bounds");
    }

    int regionW = x1 - x0;
    int regionH = y1 - y0;

    // Extract grayscale for the region from both images
    std::vector<double> regionGray1(static_cast<size_t>(regionW) * regionH);
    std::vector<double> regionGray2(static_cast<size_t>(regionW) * regionH);

    for (int ry = 0; ry < regionH; ++ry) {
        for (int rx = 0; rx < regionW; ++rx) {
            const uint8_t *p1b = img1.getPixel(x0 + rx, y0 + ry);
		    double r1 = 0.;
		    double g1 = 0.;
		    double b1 = 0.;
            // Each pixel stored as 6 bytes: R_lo,R_hi,G_lo,G_hi,B_lo,B_hi (16-bit little-endian per channel)
		    uint16_t r1v = static_cast<uint16_t>(p1b[0]) | (static_cast<uint16_t>(p1b[1]) << 8);
		    uint16_t g1v = static_cast<uint16_t>(p1b[2]) | (static_cast<uint16_t>(p1b[3]) << 8);
		    uint16_t b1v = static_cast<uint16_t>(p1b[4]) | (static_cast<uint16_t>(p1b[5]) << 8);
		    if ((r1v > 60000) && (g1v > 60000) && (b1v > 60000)) {
			    r1 = r1v / 65535.0;
			    g1 = g1v / 65535.0;
			    b1 = b1v / 65535.0;
		    }
            const uint8_t *p2b = img2.getPixel(x0 + rx, y0 + ry);
		    double r2 = 0.;
		    double g2 = 0.;
		    double b2 = 0.;
		    uint16_t r2v = static_cast<uint16_t>(p2b[0]) | (static_cast<uint16_t>(p2b[1]) << 8);
		    uint16_t g2v = static_cast<uint16_t>(p2b[2]) | (static_cast<uint16_t>(p2b[3]) << 8);
		    uint16_t b2v = static_cast<uint16_t>(p2b[4]) | (static_cast<uint16_t>(p2b[5]) << 8);
		    if ((r2v > 60000) && (g2v > 60000) && (b2v > 60000)) {
			    r2 = r2v / 65535.0;
			    g2 = g2v / 65535.0;
			    b2 = b2v / 65535.0;
		    }
            regionGray1[ry * regionW + rx] = 0.299 * r1 + 0.587 * g1 + 0.114 * b1;
            regionGray2[ry * regionW + rx] = 0.299 * r2 + 0.587 * g2 + 0.114 * b2;
        }
    }

    // Compute FFT of both region images
    std::vector<std::complex<double>> fft1 = computeFFT2D(regionGray1, regionW, regionH);
    std::vector<std::complex<double>> fft2 = computeFFT2D(regionGray2, regionW, regionH);

    // Determine padded FFT dimensions (must match computeFFT2D)
    size_t fftWidth = nextPowerOf2(static_cast<size_t>(regionW));
    size_t fftHeight = nextPowerOf2(static_cast<size_t>(regionH));

    // Compute cross-power spectrum using padded sizes
    std::vector<std::complex<double>> crossPower(fftWidth * fftHeight);
    for (size_t i = 0; i < crossPower.size(); ++i) {
        std::complex<double> product = fft1[i] * std::conj(fft2[i]);
        double magnitude = std::abs(product);
        if (magnitude > 1e-10) {
            crossPower[i] = product / magnitude;
        } else {
            crossPower[i] = std::complex<double>(0.0, 0.0);
        }
    }

    // Inverse FFT to get phase correlation surface
    std::vector<std::complex<double>> pcm = computeIFFT2D(crossPower, regionW, regionH);

    // Find peak in phase correlation matrix
    ImageShift shift = findPeak(pcm, regionW, regionH);

    // shift currently relative to region; no need to add x0,y0 unless desired
    return shift;
}

std::vector<double> PhaseCorrelation::convertToGrayscale(const RGBImage &img)
{
    std::vector<double> gray(img.width * img.height);

    for (int y = 0; y < img.height; ++y) {
	    for (int x = 0; x < img.width; ++x) {
		    const uint8_t *pb = img.getPixel(x, y);
		    double r = 0.;
		    double g = 0.;
		    double b = 0.;
		    uint16_t rv = static_cast<uint16_t>(pb[0]) | (static_cast<uint16_t>(pb[1]) << 8);
		    uint16_t gv = static_cast<uint16_t>(pb[2]) | (static_cast<uint16_t>(pb[3]) << 8);
		    uint16_t bv = static_cast<uint16_t>(pb[4]) | (static_cast<uint16_t>(pb[5]) << 8);
		    if ((rv > 60000) && (gv > 60000) && (bv > 60000)) {
			    r = rv / 65535.0;
			    g = gv / 65535.0;
			    b = bv / 65535.0;
		    }
		    gray[y * img.width + x] = 0.299 * r + 0.587 * g + 0.114 * b;
	    }
    }

    return gray;
}

void PhaseCorrelation::fft1D(std::vector<std::complex<double>> &data, bool inverse)
{
    size_t n = data.size();
    if (n <= 1)
        return;

    // Check if power of 2
    if ((n & (n - 1)) != 0) {
        throw std::invalid_argument("FFT size must be power of 2");
    }

    // Bit-reversal permutation
    for (size_t i = 0, j = 0; i < n; ++i) {
        if (j > i) {
            std::swap(data[i], data[j]);
        }
        size_t m = n >> 1;
        while (m >= 1 && j >= m) {
            j -= m;
            m >>= 1;
        }
        j += m;
    }

    // Cooley-Tukey decimation-in-time FFT
    double sign = inverse ? 1.0 : -1.0;
    for (size_t len = 2; len <= n; len <<= 1) {
        double angle = sign * 2.0 * M_PI / len;
        std::complex<double> wlen(std::cos(angle), std::sin(angle));

        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t j = 0; j < len / 2; ++j) {
                std::complex<double> u = data[i + j];
                std::complex<double> v = data[i + j + len / 2] * w;
                data[i + j] = u + v;
                data[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }

    if (inverse) {
        for (auto &val : data) {
            val /= (double)n;
        }
    }
}

size_t PhaseCorrelation::nextPowerOf2(size_t n)
{
    size_t power = 1;
    while (power < n) {
        power <<= 1;
    }
    return power;
}

std::vector<std::complex<double>> PhaseCorrelation::computeFFT2D(const std::vector<double> &input, int width, int height)
{

    // Pad to next power of 2 for efficient FFT
    size_t fftWidth = nextPowerOf2(width);
    size_t fftHeight = nextPowerOf2(height);

    std::vector<std::complex<double>> data(
        fftWidth * fftHeight, std::complex<double>(0.0, 0.0));

    // Copy input data
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            data[y * fftWidth + x] = std::complex<double>(
                input[y * width + x], 0.0);
        }
    }

    // FFT on rows
    std::vector<std::complex<double>> row(fftWidth);
    for (size_t y = 0; y < fftHeight; ++y) {
        for (size_t x = 0; x < fftWidth; ++x) {
            row[x] = data[y * fftWidth + x];
        }
        fft1D(row, false);
        for (size_t x = 0; x < fftWidth; ++x) {
            data[y * fftWidth + x] = row[x];
        }
    }

    // FFT on columns
    std::vector<std::complex<double>> col(fftHeight);
    for (size_t x = 0; x < fftWidth; ++x) {
        for (size_t y = 0; y < fftHeight; ++y) {
            col[y] = data[y * fftWidth + x];
        }
        fft1D(col, false);
        for (size_t y = 0; y < fftHeight; ++y) {
            data[y * fftWidth + x] = col[y];
        }
    }

    return data;
}

std::vector<std::complex<double>> PhaseCorrelation::computeIFFT2D(const std::vector<std::complex<double>> &input, int width, int height)
{

    size_t fftWidth = nextPowerOf2(width);
    size_t fftHeight = nextPowerOf2(height);

    std::vector<std::complex<double>> data = input;

    // IFFT on rows
    std::vector<std::complex<double>> row(fftWidth);
    for (size_t y = 0; y < fftHeight; ++y) {
        for (size_t x = 0; x < fftWidth; ++x) {
            row[x] = data[y * fftWidth + x];
        }
        fft1D(row, true);
        for (size_t x = 0; x < fftWidth; ++x) {
            data[y * fftWidth + x] = row[x];
        }
    }

    // IFFT on columns
    std::vector<std::complex<double>> col(fftHeight);
    for (size_t x = 0; x < fftWidth; ++x) {
        for (size_t y = 0; y < fftHeight; ++y) {
            col[y] = data[y * fftWidth + x];
        }
        fft1D(col, true);
        for (size_t y = 0; y < fftHeight; ++y) {
            data[y * fftWidth + x] = col[y];
        }
    }

    // Ensure output is real-valued
    for (auto &val : data) {
        val = std::complex<double>(std::abs(val), 0.0);
    }

    return data;
}

ImageShift PhaseCorrelation::findPeak(const std::vector<std::complex<double>> &pcm, int width, int height)
{
    size_t fftWidth = nextPowerOf2(width);
    size_t fftHeight = nextPowerOf2(height);

    ImageShift result;
    result.dx = 0;
    result.dy = 0;
    result.confidence = 0.0;

    double maxVal = 0.0;
    int maxX = 0;
    int maxY = 0;

    // Find maximum value in phase correlation matrix
    for (size_t y = 0; y < fftHeight; ++y) {
        for (size_t x = 0; x < fftWidth; ++x) {
            double val = std::abs(pcm[y * fftWidth + x]);
            if (val > maxVal) {
                maxVal = val;
                maxX = static_cast<int>(x);
                maxY = static_cast<int>(y);
            }
        }
    }

    // Convert to shift coordinates (handle wraparound)
    result.dx = maxX > static_cast<int>(fftWidth / 2) ? maxX - static_cast<int>(fftWidth) : maxX;
    result.dy = maxY > static_cast<int>(fftHeight / 2) ? maxY - static_cast<int>(fftHeight) : maxY;

    // Subpixel refinement using parabolic interpolation
    int xm1, x0, xp1, ym1, y0, yp1;

    // X-direction refinement
    xm1 = (maxX - 1 + fftWidth) % fftWidth;
    x0 = maxX;
    xp1 = (maxX + 1) % fftWidth;
    double offsetX = parabolicInterp(std::abs(pcm[maxY * fftWidth + xm1]), std::abs(pcm[maxY * fftWidth + x0]), std::abs(pcm[maxY * fftWidth + xp1]));

    // Y-direction refinement
    ym1 = (maxY - 1 + fftHeight) % fftHeight;
    y0 = maxY;
    yp1 = (maxY + 1) % fftHeight;
    double offsetY = parabolicInterp(std::abs(pcm[ym1 * fftWidth + maxX]), std::abs(pcm[y0 * fftWidth + maxX]), std::abs(pcm[yp1 * fftWidth + maxX]));

    result.dx += offsetX;
    result.dy += offsetY;

    result.confidence = maxVal;

    return result;
}

// Example usage function
/*
#include <iostream>

int main() {
    try {
        // Load two images (assuming you have a function to load CR3 files)
        RGBImage img1 = readCR3File("image1.cr3");
        RGBImage img2 = readCR3File("image2.cr3");
        
        // Calculate shift using phase correlation
        ImageShift shift = PhaseCorrelation::calculateShift(img1, img2);
        
        std::cout << "Image shift detected:" << std::endl;
        std::cout << "  dx: " << shift.dx << " pixels" << std::endl;
        std::cout << "  dy: " << shift.dy << " pixels" << std::endl;
        std::cout << "  confidence: " << shift.confidence << std::endl;
        
        // Positive dx means img2 is shifted right relative to img1
        // Positive dy means img2 is shifted down relative to img1
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
*/