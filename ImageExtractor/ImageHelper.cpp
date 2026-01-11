#include "ImageHelper.h"
#include <stdexcept>
#include <windows.h>

COMInitializer::COMInitializer()
{
	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
		throw std::runtime_error("Failed to initialize COM");
	}
	initialized = SUCCEEDED(hr);
}

COMInitializer::~COMInitializer()
{
	if (initialized) {
		CoUninitialize();
	}
}

std::wstring stringToWString(const std::string &str)
{
	if (str.empty())
		return std::wstring();
	int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
	std::wstring wstr(size, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size);
	return wstr;
}
