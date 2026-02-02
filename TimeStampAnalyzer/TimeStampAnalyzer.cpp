// TimeStampAnalyzer.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
// Force static library usage of NTPClient and link the library if available
#define NTPCLIENT_STATIC
#pragma comment(lib, "NTPClient.lib")

// Prevent windows.h from including winsock.h; include winsock2 first
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include "../NTPClient/NTPClient.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <time.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <windows.h>
#include <vector>
#include <limits>


uint64_t getSystemNs(std::chrono::system_clock::time_point sysTime)
{
 return static_cast<uint64_t>(
 std::chrono::duration_cast<std::chrono::nanoseconds>(
 sysTime.time_since_epoch())
 .count());
}
static inline uint64_t util_mul_div64(uint64_t num, uint64_t mul, uint64_t div)
{
#if defined(_MSC_VER) && defined(_M_X64) && (_MSC_VER >=1920)
 unsigned __int64 high;
 const unsigned __int64 low = _umul128(num, mul, &high);
 unsigned __int64 rem;
 return _udiv128(high, low, div, &rem);
#else
 const uint64_t rem = num % div;
 return (num / div) * mul + (rem * mul) / div;
#endif
}
static bool have_clockfreq = false;
static LARGE_INTEGER clock_freq;
static uint32_t winver =0;

static inline uint64_t get_clockfreq(void)
{
 if (!have_clockfreq) {
 QueryPerformanceFrequency(&clock_freq);
 have_clockfreq = true;
 }

 return clock_freq.QuadPart;
}
uint64_t os_gettime_ns(void)
{
 LARGE_INTEGER current_time;
 QueryPerformanceCounter(&current_time);
 return util_mul_div64(current_time.QuadPart,1000000000,
 get_clockfreq());
}

// Collect start offsets for each server in ntp_list. Returns vector of ("host:port", offset)
static std::vector<std::pair<std::string, double>>
collectOffsets(const std::vector<std::pair<std::string, int>> &ntp_list,
		    int samples = 5)
{
	std::vector<std::pair<std::string, double>> offsets;
	offsets.reserve(ntp_list.size());

	for (size_t idx = 0; idx < ntp_list.size(); ++idx) {
		const auto &entry = ntp_list[idx];
		const std::string &host = entry.first;
		int port = entry.second;
		double sampleOffset = std::numeric_limits<double>::quiet_NaN();
		uint64_t samplePrecise = 0;
		double sampleDelay = 0.0;
		uint64_t sampleSys = 0;

		try {
			NTPClient tmpClient(host, port);
			if (tmpClient.sampleBest(samples, sampleOffset,
						 samplePrecise, sampleDelay,
						 sampleSys)) {
				offsets.emplace_back(
					host + ":" + std::to_string(port),
					sampleOffset);
			} else {
				offsets.emplace_back(
					host + ":" + std::to_string(port),
					std::numeric_limits<double>::quiet_NaN());
			}
		} catch (...) {
			offsets.emplace_back(
				host + ":" + std::to_string(port),
				std::numeric_limits<double>::quiet_NaN());
		}
	}

	return offsets;
}

int main(int argc, char *argv[])
{
	// Default NTP server and port
	std::string ntp_server_str = "pool.ntp.org";
	int ntp_port = 123;
	// List of servers (host, port)
	std::vector<std::pair<std::string, int>> ntp_list;

	// Helper to trim whitespace and surrounding quotes
	auto trim = [](std::string s) {
		// trim spaces
		auto l = s.find_first_not_of(" \t\n\r");
		if (l == std::string::npos)
			return std::string();
		auto r = s.find_last_not_of(" \t\n\r");
		s = s.substr(l, r - l + 1);
		// remove surrounding quotes if present
		if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
			s = s.substr(1, s.size() - 2);
		return s;
	};

	// Parse command line arguments for -ntp=<server[:port]> or -port=<port>
	for (int i = 1; i < argc; ++i) {
		if (strncmp(argv[i], "-ntp=", 5) == 0) {
			std::string val = std::string(argv[i] + 5);
			// If the argument was quoted by the shell the quotes are typically removed;
			// nonetheless accept entries either quoted or not. The value may contain commas.
			val = trim(val);
			// If the first char is '"' but quotes were preserved, remove them
			if (!val.empty() && val.front() == '"' &&
			    val.back() == '"') {
				val = val.substr(1, val.size() - 2);
			}
			// split by comma
			size_t start = 0;
			while (start < val.size()) {
				size_t comma = val.find(',', start);
				std::string token;
				if (comma == std::string::npos) {
					token = val.substr(start);
					start = val.size();
				} else {
					token = val.substr(start,
							   comma - start);
					start = comma + 1;
				}
				token = trim(token);
				if (token.empty())
					continue;
				// parse host[:port]
				auto colon = token.find(':');
				std::string host;
				int port = 123;
				if (colon != std::string::npos) {
					host = token.substr(0, colon);
					std::string portstr =
						token.substr(colon + 1);
					try {
						port = std::stoi(portstr);
					} catch (...) {
						port = 123;
					}
				} else {
					host = token;
				}
				host = trim(host);
				if (!host.empty())
					ntp_list.emplace_back(host, port);
			}
		} else if (strncmp(argv[i], "-port=", 6) == 0) {
			ntp_port = std::atoi(argv[i] + 6);
		}
	}

	// If no servers parsed, use default single server
	if (ntp_list.empty()) {
		ntp_list.emplace_back(ntp_server_str, ntp_port);
	} else {
		// Use first entry for backward compatibility
		ntp_server_str = ntp_list[0].first;
		ntp_port = ntp_list[0].second;
	}

	// Allocate a C-string copy as requested (first server)
	char *ntp_server = _strdup(ntp_server_str.c_str());
	if (!ntp_server) {
		std::cerr << "Failed to allocate ntp_server string\n";
		return 1;
	}

	NTPClient *ntpClient = new NTPClient(ntp_server, ntp_port);

	double outOffsetStart = 0.0;
	double outOffsetStop = 0.0;
	uint64_t outPreciseNs = 0;
	double outDelay = 0.0;
	uint64_t outSysTime = 0;

	// Collect start offsets for all servers
	auto offsetsStartList = collectOffsets(ntp_list, 20);

	std::this_thread::sleep_for(std::chrono::seconds(15));
	auto offsetsStopList = collectOffsets(ntp_list, 20);

	// Compute and print per-server offset differences
	std::cout << "Per-server offset differences:\n";
	std::cout << "Server,Port,StartOffset,StopOffset,OffsetDiff\n";
	const size_t n = offsetsStartList.size();
	for (size_t i = 0; i < n; ++i) {
		const auto &startEntry = offsetsStartList[i];
		const auto &stopEntry = offsetsStopList[i];
		const std::string &serverStr = startEntry.first; // "host:port"
		double startVal = startEntry.second;
		double stopVal = stopEntry.second;

		// split serverStr into host and port (last colon)
		auto pos = serverStr.find_last_of(':');
		std::string host = serverStr;
		std::string portStr = "";
		if (pos != std::string::npos) {
			host = serverStr.substr(0, pos);
			portStr = serverStr.substr(pos + 1);
		}

		if (std::isnan(startVal) || std::isnan(stopVal)) {
			std::cout << host << "," << portStr << ",";
			if (std::isnan(startVal))
				std::cout << "N/A";
			else
				std::cout << startVal;
			std::cout << ",";
			if (std::isnan(stopVal))
				std::cout << "N/A";
			else
				std::cout << stopVal;
			std::cout << ",N/A" << std::endl;
		} else {
			int64_t diff = (int64_t)stopVal - (int64_t)startVal;
			std::cout << host << "," << portStr << ","
				  << (int64_t)startVal << ","
				  << (int64_t)stopVal << "," << diff
				  << std::endl;
		}
	}
	// Cleanup
	free(ntp_server);
	delete ntpClient;

	return 0;
}
