#include "RGBImage.h"
#include <vector>
#include <string>
#include <memory>
#include <stdexcept>
#include <cstdint>
#include <windows.h>
#include <comdef.h>
#include <wincodec.h>
#include "ImageHelper.h"

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

RGBImage readJPEGFile(const std::string &filename)
{
	RGBImage result;

	// Initialize COM
	COMInitializer comInit;

	// Convert filename to wide string
	std::wstring wFilename = stringToWString(filename);

	// Create WIC factory
	IWICImagingFactory *pFactory = nullptr;
	HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
				      CLSCTX_INPROC_SERVER,
				      IID_IWICImagingFactory,
				      reinterpret_cast<LPVOID *>(&pFactory));

	if (FAILED(hr)) {
		throw std::runtime_error("Failed to create WIC factory");
	}

	// Create decoder for the JPEG file
	IWICBitmapDecoder *pDecoder = nullptr;
	hr = pFactory->CreateDecoderFromFilename(wFilename.c_str(), nullptr,
						 GENERIC_READ,
						 WICDecodeMetadataCacheOnLoad,
						 &pDecoder);

	if (FAILED(hr)) {
		pFactory->Release();
		throw std::runtime_error(
			"Failed to create decoder for JPEG file");
	}

	// Get the first frame
	IWICBitmapFrameDecode *pFrame = nullptr;
	hr = pDecoder->GetFrame(0, &pFrame);

	if (FAILED(hr)) {
		pDecoder->Release();
		pFactory->Release();
		throw std::runtime_error("Failed to get frame from JPEG file");
	}

	// Get image dimensions
	UINT width, height;
	hr = pFrame->GetSize(&width, &height);

	if (FAILED(hr)) {
		pFrame->Release();
		pDecoder->Release();
		pFactory->Release();
		throw std::runtime_error("Failed to get image dimensions");
	}

	result.width = static_cast<int>(width);
	result.height = static_cast<int>(height);

	// Create format converter to 24-bit RGB (8-bit per channel)
	IWICFormatConverter *pConverter = nullptr;
	hr = pFactory->CreateFormatConverter(&pConverter);

	if (FAILED(hr)) {
		pFrame->Release();
		pDecoder->Release();
		pFactory->Release();
		throw std::runtime_error("Failed to create format converter");
	}

	// Initialize the converter to 24-bit RGB format
	hr = pConverter->Initialize(
		pFrame,
		GUID_WICPixelFormat24bppRGB, // 8-bit per channel RGB
		WICBitmapDitherTypeNone, nullptr, 0.0,
		WICBitmapPaletteTypeMedianCut);

	if (FAILED(hr)) {
		pConverter->Release();
		pFrame->Release();
		pDecoder->Release();
		pFactory->Release();
		throw std::runtime_error(
			"Failed to initialize format converter");
	}

	// Allocate buffer for pixel data
	result.data.resize(width * height * 3); // 3 channels (RGB)

	// Calculate stride (bytes per row)
	UINT stride = width * 3 * sizeof(uint8_t);
	UINT bufferSize = stride * height;

	// Copy pixels to our buffer
	hr = pConverter->CopyPixels(
		nullptr, // Copy entire image
		stride, bufferSize,
		reinterpret_cast<BYTE *>(result.data.data()));

	if (FAILED(hr)) {
		pConverter->Release();
		pFrame->Release();
		pDecoder->Release();
		pFactory->Release();
		throw std::runtime_error("Failed to copy pixel data");
	}

	// Clean up
	pConverter->Release();
	pFrame->Release();
	pDecoder->Release();
	pFactory->Release();

	return result;
}