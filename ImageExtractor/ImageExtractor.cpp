#include "RGBImage.h"
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <string>
#include <iostream>
#include <vector>
#include <shlwapi.h>
#include <cmath>
#include <algorithm>
#include "StackedImage.h"

#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "shlwapi.lib")

// forward declaration for CR3 reader implemented in CR3toRGBImage.cpp
RGBImage readCR3File(const std::string &filename);

// Helper function to check if file exists
bool FileExists(const std::string &filename)
{
	DWORD attrib = GetFileAttributesA(filename.c_str());
	return (attrib != INVALID_FILE_ATTRIBUTES &&
		!(attrib & FILE_ATTRIBUTE_DIRECTORY));
}

// Helper function to create directory
bool CreateDirectoryRecursive(const std::string &path)
{
	if (CreateDirectoryA(path.c_str(), NULL) ||
	 GetLastError() == ERROR_ALREADY_EXISTS) {
		return true;
	}
	return false;
}

// Helper function to extract directory path from full path
std::string GetDirectoryPath(const std::string &filepath)
{
	size_t pos = filepath.find_last_of("\\/");
	if (pos != std::string::npos) {
		return filepath.substr(0, pos);
	}
	return "";
}

// Helper function to get filename without extension
std::string GetFilenameWithoutExtension(const std::string &filepath)
{
	size_t lastSlash = filepath.find_last_of("\\/");
	size_t lastDot = filepath.find_last_of(".");

	size_t start = (lastSlash != std::string::npos) ? lastSlash +1 :0;
	size_t end = (lastDot != std::string::npos && lastDot > start)
			 ? lastDot
			 : filepath.length();

	return filepath.substr(start, end - start);
}

// Helper: get file extension in lowercase including the dot, or empty string
std::string GetExtensionLowercase(const std::string &filepath)
{
	size_t lastDot = filepath.find_last_of('.');
	if (lastDot == std::string::npos)
		return std::string();
	std::string ext = filepath.substr(lastDot);
	for (char &c : ext) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return ext;
}

// Helper function to save frame as BMP
bool SaveFrameAsBMP(const std::string &filename, BYTE *data, UINT32 width,
		 UINT32 height, UINT32 stride)
{
	BITMAPFILEHEADER fileHeader = {};
	BITMAPINFOHEADER infoHeader = {};

	// Setup info header
	infoHeader.biSize = sizeof(BITMAPINFOHEADER);
	infoHeader.biWidth = width;
	infoHeader.biHeight = -(LONG)height; // Negative for top-down bitmap
	infoHeader.biPlanes =1;
	infoHeader.biBitCount =32; // BGRA
	infoHeader.biCompression = BI_RGB;
	infoHeader.biSizeImage = stride * height;

	// Setup file header
	fileHeader.bfType =0x4D42; // 'BM'
	fileHeader.bfOffBits =
		sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
	fileHeader.bfSize = fileHeader.bfOffBits + infoHeader.biSizeImage;

	HANDLE hFile = CreateFileA(filename.c_str(), GENERIC_WRITE,0, NULL,
				 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		return false;
	}

	DWORD written;
	WriteFile(hFile, &fileHeader, sizeof(fileHeader), &written, NULL);
	WriteFile(hFile, &infoHeader, sizeof(infoHeader), &written, NULL);
	WriteFile(hFile, data, infoHeader.biSizeImage, &written, NULL);

	CloseHandle(hFile);
	return true;
}

// Save an RGBImage (16-bit per channel R,G,B) to a24-bit BMP file (8-bit per channel)
bool SaveRGBImageAsBMP(const std::string &filename, const RGBImage &img)
{
	if (img.width <= 0 || img.height <= 0)
		return false;

	const int w = img.width;
	const int h = img.height;

	//24-bit BMP stride (rows padded to4-byte boundary)
	int rowBytes = w * 3;
	int stride = ((rowBytes + 3) / 4) * 4;
	size_t bufferSize =
		static_cast<size_t>(stride) * static_cast<size_t>(h);

	std::vector<BYTE> buffer(bufferSize);
	// img.data is RGB triplets (uint8_t each). Convert to BGR8-bit for BMP
	const uint8_t *src = img.data.data();

	// Copy pixels from src (RGB) into buffer (BGR) respecting stride and padding.
	// The BMP info header uses a negative height for a top-down bitmap, so rows
	// are written top-to-bottom: row0 is the first row in the buffer.
	for (int y = 0; y < h; ++y) {
		BYTE *dstRow =
			buffer.data() +
			static_cast<size_t>(y) * static_cast<size_t>(stride);
		const uint8_t *srcRow = src + static_cast<size_t>(y) *
						      static_cast<size_t>(w) *
						      3;
		for (int x = 0; x < w; ++x) {
			// src: R,G,B -> BMP: B,G,R
			dstRow[x * 3 + 0] = srcRow[x * 3 + 2]; // B
			dstRow[x * 3 + 1] = srcRow[x * 3 + 1]; // G
			dstRow[x * 3 + 2] = srcRow[x * 3 + 0]; // R
		}
		// padding bytes (if any) are left as zero because vector was value-initialized
	}

	BITMAPFILEHEADER fileHeader = {};
	BITMAPINFOHEADER infoHeader = {};

	infoHeader.biSize = sizeof(BITMAPINFOHEADER);
	infoHeader.biWidth = w;
	// negative height for top-down bitmap
	infoHeader.biHeight = -h;
	infoHeader.biPlanes = 1;
	infoHeader.biBitCount = 24;
	infoHeader.biCompression = BI_RGB;
	infoHeader.biSizeImage = static_cast<UINT32>(bufferSize);

	fileHeader.bfType = 0x4D42; // 'BM'
	fileHeader.bfOffBits =
		sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
	fileHeader.bfSize = fileHeader.bfOffBits + infoHeader.biSizeImage;

	HANDLE hFile = CreateFileA(filename.c_str(), GENERIC_WRITE, 0, NULL,
				   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		return false;
	}

	DWORD written = 0;
	BOOL ok = WriteFile(hFile, &fileHeader, sizeof(fileHeader), &written,
			    NULL);
	ok = ok &&
	     WriteFile(hFile, &infoHeader, sizeof(infoHeader), &written, NULL);
	ok = ok && WriteFile(hFile, buffer.data(),
			     static_cast<DWORD>(bufferSize), &written, NULL);

	CloseHandle(hFile);
	return ok == TRUE;
}

bool ExtractMP4Frames(const std::string &mp4Filename)
{
	HRESULT hr = S_OK;
	IMFSourceReader *pReader = NULL;
	IMFMediaType *pType = NULL;
	IMFSample *pSample = NULL;
	IMFMediaBuffer *pBuffer = NULL;

	// Initialize Media Foundation
	hr = MFStartup(MF_VERSION);
	if (FAILED(hr)) {
		std::cerr << "Failed to initialize Media Foundation"
			 << std::endl;
		return false;
	}

	// Convert string to wstring for Windows API
	std::wstring wFilename(mp4Filename.begin(), mp4Filename.end());

	IMFAttributes* pAttributes = nullptr;
	hr = MFCreateAttributes(&pAttributes,1);
	if (SUCCEEDED(hr)) {
	 // Allow hardware transforms (decoders) - helps the reader find a decoder
	 pAttributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
	 // Optionally allow video processing (color convert, scaling):
	 pAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
	}

	// Create source reader
	hr = MFCreateSourceReaderFromURL(wFilename.c_str(), pAttributes, &pReader);
	if (pAttributes) pAttributes->Release();
	if (FAILED(hr)) {
		std::cerr << "Failed to create source reader" << std::endl;
		MFShutdown();
		return false;
	}

	// Configure the source reader to convert video to BGRA32
	hr = MFCreateMediaType(&pType);
	if (SUCCEEDED(hr)) {
		hr = pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	}
	if (SUCCEEDED(hr)) {
		hr = pType->SetGUID(MF_MT_SUBTYPE,
				MFVideoFormat_RGB32); // Request RGB32 (BGRA)
	}
	if (SUCCEEDED(hr)) {
		hr = pReader->SetCurrentMediaType(
			(DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL,
			pType);
		if (FAILED(hr)) {
			// Print diagnostic and attempt a common alternative (ARGB32)
			std::cerr << "SetCurrentMediaType(MFVideoFormat_RGB32) failed:0x" << std::hex << hr << std::dec << std::endl;
			HRESULT hr2 = pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32);
			if (SUCCEEDED(hr2)) {
				hr2 = pReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pType);
				if (SUCCEEDED(hr2)) {
					hr = hr2; // success with fallback
				} else {
					std::cerr << "SetCurrentMediaType(MFVideoFormat_ARGB32) also failed:0x" << std::hex << hr2 << std::dec << std::endl;
				}
			} else {
				std::cerr << "Failed to set MF_MT_SUBTYPE to ARGB32:0x" << std::hex << hr2 << std::dec << std::endl;
			}
		}
	}

	if (FAILED(hr)) {
		std::cerr << "Failed to set media type (no suitable conversion available). HRESULT=0x" << std::hex << hr << std::dec << std::endl;
		if (pType) { pType->Release(); pType = NULL; }
		if (pReader) { pReader->Release(); pReader = NULL; }
		MFShutdown();
		return false;
	}

	pType->Release();
	pType = NULL;

	// Get the actual media type after conversion
	hr = pReader->GetCurrentMediaType(
		(DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pType);
	if (FAILED(hr)) {
		std::cerr << "Failed to get current media type" << std::endl;
		pReader->Release();
		MFShutdown();
		return false;
	}

	// Get frame dimensions and stride
	UINT32 width =0, height =0;
	MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height);

	LONG stride =0;
	hr = pType->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32 *)&stride);
	if (FAILED(hr)) {
		stride = width *4; //4 bytes per pixel for BGRA
	}
	stride = abs(stride);

	pType->Release();

	// Create output directory
	std::string parentDir = GetDirectoryPath(mp4Filename);
	std::string stemName = GetFilenameWithoutExtension(mp4Filename);

	std::string outputDir;
	if (!parentDir.empty()) {
		outputDir = parentDir + "\\" + stemName;
	} else {
		outputDir = stemName;
	}

	if (!CreateDirectoryRecursive(outputDir)) {
		std::cerr << "Failed to create output directory" << std::endl;
		pReader->Release();
		MFShutdown();
		return false;
	}

	std::cout << "Extracting frames to: " << outputDir << std::endl;
	std::cout << "Video resolution: " << width << "x" << height
		 << std::endl;

	// Extract frames
	int frameCount =0;
	while (true) {
		DWORD streamFlags =0;
		LONGLONG timestamp =0;

		hr = pReader->ReadSample(
			(DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,0, NULL,
			&streamFlags, &timestamp, &pSample);

		if (FAILED(hr)) {
			break;
		}

		if (streamFlags & MF_SOURCE_READERF_ENDOFSTREAM) {
			break;
		}

		if (pSample == NULL) {
			continue;
		}

		// Get buffer from sample
		hr = pSample->ConvertToContiguousBuffer(&pBuffer);
		if (SUCCEEDED(hr)) {
			BYTE *pData = NULL;
			DWORD dataLength =0;

			hr = pBuffer->Lock(&pData, NULL, &dataLength);
			if (SUCCEEDED(hr)) {
				// Save frame
				char filename[512];
				sprintf_s(filename, "%s\\frame_%06d.bmp",
					 outputDir.c_str(), frameCount);

				if (SaveFrameAsBMP(filename, pData, width,
						 height, stride)) {
					frameCount++;
					if (frameCount %30 ==0) {
						std::cout << "Extracted "
							 << frameCount
							 << " frames..."
							 << std::endl;
					}
					if (frameCount >=100) {
						std::cout
							<< "Reached1000 frames, stopping extraction."
							<< std::endl;
						pBuffer->Unlock();
						break;
					}
				}

				pBuffer->Unlock();
			}

			pBuffer->Release();
			pBuffer = NULL;
		}

		pSample->Release();
		pSample = NULL;
	}

	std::cout << "Extraction complete. Total frames: " << frameCount
		 << std::endl;

	// Cleanup
	if (pReader)
		pReader->Release();
	MFShutdown();

	return frameCount >0;
}

// Compute shift in white pixels between two images.
// Returns pair(dx, dy) where dx = centerB.x - centerA.x, dy = centerB.y - centerA.y
// A pixel is considered "white" when max(R,G,B) >= whiteThreshold (16-bit value).
// If images have different sizes or no white pixels found in either image, (0,0) is returned.
std::pair<double, double> ComputeWhiteShift(RGBImage &imgA,
				      RGBImage &imgB,
					  const int x0,
					  const int y0,
					  const int sx,
	                  const int sy,
				      uint8_t whiteThreshold = 200)
{
	if (imgA.width != imgB.width || imgA.height != imgB.height) {
		std::cerr << "ComputeWhiteShift: image sizes differ"
			  << std::endl;
		return {0, 0};
	}

	int w = imgA.width;
	int h = imgA.height;
	size_t total = static_cast<size_t>(w) * static_cast<size_t>(h);

	if (((x0 - (sx/2)) < 0) || ((x0 + (sx/2)) >= w) || 
		((y0 - (sy/2)) < 0) || ((y0 + (sy/2)) >= h)) {
		std::cerr << "Sub range is not in range of image size"
			  << std::endl;
	}

	double sumAx = 0.0, sumAy = 0.0;
	double sumBx = 0.0, sumBy = 0.0;
	size_t countA = 0, countB = 0;
  
	for (int x = x0 - (sx/2); x < x0 + (sx/2); x++) {
		for (int y = y0 - (sy/2); y < y0 + (sy/2); y++) {
			uint8_t *a = imgA.getPixel(x, y);
			uint8_t *b = imgB.getPixel(x, y);

			uint8_t ar = a[0];
			uint8_t ag = a[1];
			uint8_t ab = a[2];
			uint8_t br = b[0];
			uint8_t bg = b[1];
			uint8_t bb = b[2];

			// Consider pixel white only if all channels are >= threshold
			if (ar >= whiteThreshold && ag >= whiteThreshold &&
			    ab >= whiteThreshold) {
				sumAx += x;
				sumAy += y;
				++countA;
			}

			if (br >= whiteThreshold && bg >= whiteThreshold &&
			    bb >= whiteThreshold) {
				sumBx += x;
				sumBy += y;
				++countB;
			}
		}
	}

	if (countA == 0 || countB == 0) {
		std::cerr
			<< "ComputeWhiteShift: no white pixels found in one or both images"
			<< std::endl;
		return {0, 0};
	}

	std::cout << "ComputeWhiteShift: countA=" << countA
		  << ", countB=" << countB << std::endl;	

	double centerAx = sumAx / static_cast<double>(countA);
	double centerAy = sumAy / static_cast<double>(countA);
	double centerBx = sumBx / static_cast<double>(countB);
	double centerBy = sumBy / static_cast<double>(countB);

	double dx = centerBx - centerAx;
	double dy = centerBy - centerAy;

	return {dx, dy};
}

// Example usage
int main(int argc, char *argv[])
{
	if (argc <2) {
		std::cout << "Usage: " << argv[0] << " <mp4_file or cr3 file or folder>" << std::endl;
		return 1;
	}

	std::string inputPath = argv[1];

	DWORD attr = GetFileAttributesA(inputPath.c_str());
	if (attr == INVALID_FILE_ATTRIBUTES) {
		std::cerr << "Error: Path does not exist: " << inputPath << std::endl;
		return 1;
	}

	if (attr & FILE_ATTRIBUTE_DIRECTORY) {
		// It's a directory: find all .cr3 files and process them in sorted order
		// enumerate all files and filter by extension case-insensitively
		std::string pattern = inputPath;
		if (!pattern.empty() && pattern.back() != '\\' && pattern.back() != '/')
			pattern += "\\";
		pattern += "*.*";

		WIN32_FIND_DATAA findData;
		HANDLE hFind = FindFirstFileA(pattern.c_str(), &findData);
		if (hFind == INVALID_HANDLE_VALUE) {
			std::cerr << "No files found in folder: " << inputPath << std::endl;
			return 1;
		}

		std::vector<std::string> files;
		do {
			if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
				// build full path
				std::string full = inputPath;
				if (!full.empty() && full.back() != '\\' && full.back() != '/')
					full += "\\";
				full += findData.cFileName;
				// filter by extension case-insensitively
				if (GetExtensionLowercase(full) == ".cr3") {
					files.push_back(full);
				}
			}
		} while (FindNextFileA(hFind, &findData) !=0);
		FindClose(hFind);

		if (files.empty()) {
			std::cerr << "No .cr3 files found in folder: " << inputPath << std::endl;
			return 1;
		}

		std::sort(files.begin(), files.end());

		RGBImage prevImg;
		bool havePrev = false;

		double startx = 727.;
		double starty = 1226.;

		double thisx = startx;
		double thisy = starty;

		StackedImage stackedImg;

		// Find out total shift of all files
		for (const auto &f : files) {
			try {
				std::cout << "Loading: " << f << std::endl;
				RGBImage img = readCR3File(f);

			
				std::cout << "Loaded: " << f << " (" << img.width << "x" << img.height << ")" << std::endl;


				if (havePrev) {
					// Calculate shift using phase correlation
					auto shift = ComputeWhiteShift(prevImg, img, (int)thisx, (int)thisy, 300, 300);

					std::cout << "Image shift detected:";
					std::cout << "  dx: " << shift.first;
					std::cout << "  dy: " << shift.second
						  << std::endl;
					thisx += shift.first;
					thisy += shift.second;

				
				} 

				RGBImage croppedImg = img.crop((int)thisx - 300,
							    (int)thisy - 300,
							    (int)thisx + 300,
							    (int)thisy + 300);

				stackedImg.addLayer(croppedImg);

				// Save the cropped image into the inputPath folder with suffix ".cropped.bmp"
				try {
					std::string baseName =
						GetFilenameWithoutExtension(f);
					std::string outPath = inputPath;
					if (!outPath.empty() &&
					    outPath.back() != '\\' &&
					    outPath.back() != '/')
						outPath += "\\";
					outPath += baseName + ".cropped.bmp";
					if (!SaveRGBImageAsBMP(outPath, croppedImg)) {
						std::cerr
							<< "Error saving cropped image: "
							<< outPath << std::endl;
					} else {
						std::cout
							<< "Saved cropped image: "
							<< outPath << std::endl;
					}
				} catch (const std::exception &e) {
					std::cerr
						<< "Failed to save cropped image: "
						<< e.what() << std::endl;
				}

				prevImg = std::move(img);
				havePrev = true;
			} catch (const std::exception &e) {
				std::cerr << "Failed to load CR3 '" << f << "': " << e.what() << std::endl;
				// continue processing other files
			}
		}

		std::cout << "Total image shift:";
					std::cout << "  dx: " << thisx - startx;
					std::cout << "  dy: " << thisy - starty
						  << std::endl;

		RGBImage peakImg =
			stackedImg.getPeakImage();

		// Save peak image to the input directory
		std::string peakPath;
		if (!inputPath.empty() && (inputPath.back() == '\\' || inputPath.back() == '/')) {
			peakPath = inputPath + "peak_image.bmp";
		} else if (!inputPath.empty()) {
			peakPath = inputPath + "\\peak_image.bmp";
		} else {
			peakPath = "peak_image.bmp";
		}

		if (!SaveRGBImageAsBMP(peakPath, peakImg)) {
			std::cerr << "Error saving peak image: " << peakPath << std::endl;
		} else {
			std::cout << "Saved peak image: " << peakPath << std::endl;
		}

		return 0;
	}

	// Not a directory: fall back to original behavior
	std::string ext = GetExtensionLowercase(inputPath);
	if (ext == ".cr3") {
		try {
			RGBImage img = readCR3File(inputPath);
			std::cout << "Loaded CR3 image: " << inputPath << " "
				  << img.width << "x" << img.height
				  << std::endl;

			char filename[512];
			sprintf_s(filename, "%s.bmp", inputPath.c_str());

			if (!SaveRGBImageAsBMP(filename, img)) {
				std::cerr << "Error saving BMP file: " << filename << std::endl;
			}

			return 0;
		} catch (const std::exception &e) {
			std::cerr << "Failed to load CR3: " << e.what() << std::endl;
			return 1;
		}
	}

	// Otherwise treat as MP4/video file
	if (ExtractMP4Frames(inputPath)) {
		std::cout << "Success!" << std::endl;
		return 0;
	} else {
		std::cerr << "Failed to extract frames" << std::endl;
		return 1;
	}
}
