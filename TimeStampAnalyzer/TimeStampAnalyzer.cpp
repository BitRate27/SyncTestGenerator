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

int main(int argc, char *argv[])
{
	// Default NTP server and port
	std::string ntp_server_str = "pool.ntp.org";
	int ntp_port =123;

	// Parse command line arguments for -ntp=<server[:port]> or -port=<port>
	for (int i =1; i < argc; ++i) {
		if (strncmp(argv[i], "-ntp=",5) ==0) {
			std::string val = argv[i] +5;
			auto pos = val.find(':');
			if (pos != std::string::npos) {
				ntp_server_str = val.substr(0, pos);
				ntp_port = std::atoi(val.c_str() + pos +1);
			} else {
				ntp_server_str = val;
			}
		} else if (strncmp(argv[i], "-port=",6) ==0) {
			ntp_port = std::atoi(argv[i] +6);
		}
	}

	// Allocate a C-string copy as requested
	char *ntp_server = _strdup(ntp_server_str.c_str());
	if (!ntp_server) {
		std::cerr << "Failed to allocate ntp_server string\n";
		return 1;
	}

	std::cout << "Using NTP server: " << ntp_server << " port: " << ntp_port
		 << "\n";

	NTPClient *ntpClient = new NTPClient(ntp_server, ntp_port);
	uint64_t roundTripDelay1 = 0;
	uint64_t roundTripDelay2 = 0;
	uint64_t startntptime = getSystemNs(std::chrono::system_clock::now());
	uint64_t ntptimestart = ntpClient->getNetworkTimeNow(roundTripDelay1);
	std::this_thread::sleep_for(std::chrono::seconds(1));
	uint64_t ntptimestop = ntpClient->getNetworkTimeNow(roundTripDelay2);
	uint64_t stopntptime = getSystemNs(std::chrono::system_clock::now());

	/*
	uint64_t systimestart = os_gettime_ns();
	std::this_thread::sleep_for(std::chrono::seconds(1));
	uint64_t systimestop = os_gettime_ns();
	*/

	uint64_t chronotimestart =
		getSystemNs(std::chrono::system_clock::now());
	std::this_thread::sleep_for(std::chrono::seconds(1));
	uint64_t chronotimestop = getSystemNs(std::chrono::system_clock::now());

	std::cout << "      Get NTP Time," << startntptime << "," << stopntptime
		  << std::endl;
	std::cout << "          NTP Time," << ntptimestart << "," << ntptimestop
		 << std::endl;
	//std::cout << "       System Time," << systimestart << "," << systimestop
	//	 << std::endl;	
	std::cout << "       Chrono Time," << chronotimestart << "," << chronotimestop
		 << std::endl;
	std::cout << "    Get NTP diff = " << (stopntptime - startntptime - roundTripDelay1 - roundTripDelay2)
		  << " ns\n";

	std::cout << "   NTP Time diff = "
		  << (ntptimestop - ntptimestart /* - roundTripDelay1 - roundTripDelay2 */)
		 << " ns\n";
	//std::cout << "System Time diff = " << (systimestop - systimestart)
	//	 << " ns\n";
	std::cout << "Chrono Time diff = " << (chronotimestop - chronotimestart)
		 << " ns\n";

	// Cleanup
	free(ntp_server);
	delete ntpClient;

	return 0;
}
