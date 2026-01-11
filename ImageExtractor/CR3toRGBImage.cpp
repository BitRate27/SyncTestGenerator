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


RGBImage readCR3File(const std::string &filename)
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

	// Create decoder for the image file
	IWICBitmapDecoder *pDecoder = nullptr;
	hr = pFactory->CreateDecoderFromFilename(wFilename.c_str(), nullptr,
					 GENERIC_READ,
					 WICDecodeMetadataCacheOnLoad,
					 &pDecoder);

	if (FAILED(hr)) {
		pFactory->Release();
		throw std::runtime_error(
			"Failed to create decoder for CR3 file. Make sure Windows supports RAW codecs.");
	}

	// Get the first frame
	IWICBitmapFrameDecode *pFrame = nullptr;
	hr = pDecoder->GetFrame(0, &pFrame);

	if (FAILED(hr)) {
		pDecoder->Release();
		pFactory->Release();
		throw std::runtime_error("Failed to get frame from CR3 file");
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

	// Create format converter to RGB48-bit (16-bit per channel)
	IWICFormatConverter *pConverter = nullptr;
	hr = pFactory->CreateFormatConverter(&pConverter);

	if (FAILED(hr)) {
		pFrame->Release();
		pDecoder->Release();
		pFactory->Release();
		throw std::runtime_error("Failed to create format converter");
	}

	// Initialize the converter to RGB48-bit format
	hr = pConverter->Initialize(
		pFrame,
		GUID_WICPixelFormat24bppRGB, //8-bit per channel RGB
		WICBitmapDitherTypeNone, nullptr,0.0,
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
	result.data.resize(width * height *3); //3 channels (RGB)

	// Calculate stride (bytes per row)
	UINT stride = width *3 * sizeof(uint8_t);
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

// Version with additional options for RAW development
RGBImage readCR3FileWithOptions(const std::string &filename,
			double exposureCompensation =0.0)
{
	RGBImage result;

	COMInitializer comInit;
	std::wstring wFilename = stringToWString(filename);

	IWICImagingFactory *pFactory = nullptr;
	HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
				 CLSCTX_INPROC_SERVER,
				 IID_IWICImagingFactory,
				 reinterpret_cast<LPVOID *>(&pFactory));

	if (FAILED(hr)) {
		throw std::runtime_error("Failed to create WIC factory");
	}

	IWICBitmapDecoder *pDecoder = nullptr;
	hr = pFactory->CreateDecoderFromFilename(wFilename.c_str(), nullptr,
					 GENERIC_READ,
					 WICDecodeMetadataCacheOnLoad,
					 &pDecoder);

	if (FAILED(hr)) {
		pFactory->Release();
		throw std::runtime_error("Failed to create decoder");
	}

	IWICBitmapFrameDecode *pFrame = nullptr;
	hr = pDecoder->GetFrame(0, &pFrame);

	if (FAILED(hr)) {
		pDecoder->Release();
		pFactory->Release();
		throw std::runtime_error("Failed to get frame");
	}

	// Try to get development options interface
	IWICDevelopRaw *pDevelopRaw = nullptr;
	hr = pFrame->QueryInterface(IID_IWICDevelopRaw,
				 reinterpret_cast<void **>(&pDevelopRaw));

	if (SUCCEEDED(hr)) {
		// Set exposure compensation if supported
		pDevelopRaw->SetExposureCompensation(exposureCompensation);

		// Load default parameters
		pDevelopRaw->LoadParameterSet(
			WICAsShotParameterSet);
	}

	UINT width, height;
	hr = pFrame->GetSize(&width, &height);

	if (FAILED(hr)) {
		if (pDevelopRaw)
			pDevelopRaw->Release();
		pFrame->Release();
		pDecoder->Release();
		pFactory->Release();
		throw std::runtime_error("Failed to get dimensions");
	}

	result.width = static_cast<int>(width);
	result.height = static_cast<int>(height);

	IWICFormatConverter *pConverter = nullptr;
	hr = pFactory->CreateFormatConverter(&pConverter);

	if (FAILED(hr)) {
		if (pDevelopRaw)
			pDevelopRaw->Release();
		pFrame->Release();
		pDecoder->Release();
		pFactory->Release();
		throw std::runtime_error("Failed to create converter");
	}

	hr = pConverter->Initialize(pFrame, GUID_WICPixelFormat48bppRGB,
				 WICBitmapDitherTypeNone, nullptr,0.0,
				 WICBitmapPaletteTypeMedianCut);

	if (FAILED(hr)) {
		pConverter->Release();
		if (pDevelopRaw)
			pDevelopRaw->Release();
		pFrame->Release();
		pDecoder->Release();
		pFactory->Release();
		throw std::runtime_error("Failed to initialize converter");
	}

	result.data.resize(width * height *3);
	UINT stride = width *3 * sizeof(uint16_t);
	UINT bufferSize = stride * height;

	hr = pConverter->CopyPixels(
		nullptr, stride, bufferSize,
		reinterpret_cast<BYTE *>(result.data.data()));

	if (FAILED(hr)) {
		pConverter->Release();
		if (pDevelopRaw)
			pDevelopRaw->Release();
		pFrame->Release();
		pDecoder->Release();
		pFactory->Release();
		throw std::runtime_error("Failed to copy pixels");
	}

	pConverter->Release();
	if (pDevelopRaw)
		pDevelopRaw->Release();
	pFrame->Release();
	pDecoder->Release();
	pFactory->Release();

	return result;
}
