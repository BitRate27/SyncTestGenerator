#pragma once

#include <cstdint>
#include <string>
#include <chrono>
#include <mutex>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <ctime>
#include <iomanip>
#include <cmath>
#include <limits>
#include <thread>
#include <atomic>
#include <cstring>
#pragma comment(lib, "ws2_32.lib")
// Define export macro for Windows DLL. When building the NTPClient project as a DLL,
// add NTPCLIENT_EXPORTS to the project preprocessor definitions to export symbols.
// When building/using as a static library, define NTPCLIENT_STATIC so no dllimport/dllexport is used.
#if defined(NTPCLIENT_STATIC)
 #define NTPCLIENT_API
#elif defined(_WIN32)
 #if defined(NTPCLIENT_EXPORTS)
 #define NTPCLIENT_API __declspec(dllexport)
 #else
 #define NTPCLIENT_API __declspec(dllimport)
 #endif
#else
 #define NTPCLIENT_API
#endif

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

// NTP packet structure (48 bytes)
struct NTPPacket {
 uint8_t li_vn_mode; // Leap Indicator, Version, Mode
 uint8_t stratum; // Stratum level
 uint8_t poll; // Poll interval
 uint8_t precision; // Precision
 uint32_t rootDelay; // Root delay
 uint32_t rootDispersion; // Root dispersion
 uint32_t refId; // Reference ID
 uint32_t refTm_s; // Reference timestamp (seconds)
 uint32_t refTm_f; // Reference timestamp (fraction)
 uint32_t origTm_s; // Origin timestamp (seconds)
 uint32_t origTm_f; // Origin timestamp (fraction)
 uint32_t rxTm_s; // Receive timestamp (seconds)
 uint32_t rxTm_f; // Receive timestamp (fraction)
 uint32_t txTm_s; // Transmit timestamp (seconds)
 uint32_t txTm_f; // Transmit timestamp (fraction)
};

// NTP epoch starts on Jan1,1900
// Unix epoch starts on Jan1,1970
static constexpr uint64_t NTP_UNIX_OFFSET =2208988800ULL;

// NTP client class declaration
class NTPCLIENT_API NTPClient {
public:
	explicit NTPClient(const std::string &server = "", int port = -1);
	~NTPClient();

	// Get network time sample: returns offset (seconds) and roundTripDelay (seconds)
	bool getNetworkOffsetFromChronoNow(double &offset,
					   double &roundTripDelay);

	// Get the current estimated network time (ns since Unix epoch) and round-trip time (ns)
	uint64_t getNetworkTimeNow(uint64_t &rt);

	// Sample the NTP server multiple times and pick the sample with minimum RTT.
	// Returns true and fills outOffset, outPreciseNs, outDelay, outSysTime on success.
	bool sampleBest(int samples, double &outOffset, uint64_t &outPreciseNs,
			double &outDelay, uint64_t &outSysTime,
			int sleepMs = 100);

	bool initialize();

	// Update base sync if diff (ns) is better
	void syncSystemToNTP(uint64_t diff, uint64_t systemTime,
			     uint64_t ntpTime);

	// Calculate NTP time (ns since Unix epoch) based on current system time and stored base
	uint64_t calcNTPTimeAtSystemTime(uint64_t sysTime);

	bool isInitialized() const { return initialized_; }

	// Helper to get system time in ns
	uint64_t getSystemNs(std::chrono::system_clock::time_point sysTime);

private:
	std::string server_;
	int port_;
	SOCKET socket_;
	struct sockaddr_in serverAddr_;
	uint64_t basePreciseNs; // nanoseconds since Unix epoch at sync
	uint64_t baseSystemTimeNs;
	uint64_t bestDiff;
	std::mutex baseMutex;
	bool initialized_;

	// Initialize Winsock and resolve server address

	uint64_t ntpToUint64(uint32_t seconds, uint32_t fraction);
	int64_t ntpDiff(uint64_t a, uint64_t b);
};

// Returns nanoseconds since Unix epoch (1970) using NTP-synchronized clock estimate.
// The function is thread-safe and will start a background updater on first call.
NTPCLIENT_API uint64_t getAccurateNetworkTime(const std::string &ntpServer = "pool.ntp.org", int port =123);
