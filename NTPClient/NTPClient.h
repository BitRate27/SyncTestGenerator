#pragma once

#include <cstdint>
#include <string>

// Define export macro for Windows DLL. When building the NTPClient project as a DLL,
// add NTPCLIENT_EXPORTS to the project preprocessor definitions to export symbols.
#if defined(_WIN32)
 #if defined(NTPCLIENT_EXPORTS)
 #define NTPCLIENT_API __declspec(dllexport)
 #else
 #define NTPCLIENT_API __declspec(dllimport)
 #endif
#else
 #define NTPCLIENT_API
#endif

// Returns nanoseconds since Unix epoch (1970) using NTP-synchronized clock estimate.
// The function is thread-safe and will start a background updater on first call.
NTPCLIENT_API uint64_t getAccurateNetworkTime(const std::string &ntpServer = "pool.ntp.org", int port =123);

