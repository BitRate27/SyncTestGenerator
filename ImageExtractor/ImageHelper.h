#pragma once

#include <string>

// Helper class for COM initialization used by image readers
class COMInitializer {
public:
 COMInitializer();
 ~COMInitializer();

 COMInitializer(const COMInitializer&) = delete;
 COMInitializer& operator=(const COMInitializer&) = delete;

private:
 bool initialized;
};

// Convert std::string (UTF-8) to std::wstring for Windows APIs
std::wstring stringToWString(const std::string &str);
