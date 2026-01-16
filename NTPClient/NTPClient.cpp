#include "pch.h"
#define NTPCLIENT_EXPORTS
#include "NTPClient.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <cstdint>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <string>
#include <cmath>
#include <limits>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

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
const uint64_t NTP_UNIX_OFFSET =2208988800ULL;

class NTPClient {
public:
	NTPClient(const std::string &server = "pool.ntp.org", int port =123)
		: server_(server),
		 port_(port),
		 socket_(INVALID_SOCKET)
	{
		bestDiff = 10000000000ULL;
	}

	~NTPClient()
	{
		if (socket_ != INVALID_SOCKET) {
			closesocket(socket_);
		}
		WSACleanup();
	}

	bool initialize()
	{
		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2,2), &wsaData) !=0) {
			std::cerr << "WSAStartup failed\n";
			return false;
		}

		socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (socket_ == INVALID_SOCKET) {
			std::cerr << "Socket creation failed\n";
			return false;
		}

		// Set socket timeout
		DWORD timeout =5000; //5 seconds
		setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
			 sizeof(timeout));

		// Resolve server address
		struct addrinfo hints = {0}, *result = nullptr;
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_DGRAM;

		if (getaddrinfo(server_.c_str(), std::to_string(port_).c_str(),
				&hints, &result) !=0) {
			std::cerr << "Failed to resolve server address\n";
			return false;
		}

		serverAddr_ = *(struct sockaddr_in *)result->ai_addr;
		freeaddrinfo(result);

		return true;
	}

	bool getNetworkTime(double &offset, double &roundTripDelay)
	{
		NTPPacket packet;
		std::memset(&packet,0, sizeof(packet));

		// Set up NTP request packet
		packet.li_vn_mode =0x1B; // LI=0, VN=3, Mode=3 (client)

		// T1: Client transmit timestamp (origin)
		uint64_t t1 =
			getSystemNs(std::chrono::system_clock::now());

		// embed t1 transmit timestamp into packet (network order)
		uint32_t tx_s = htonl(static_cast<uint32_t>(t1 >>32));
		uint32_t tx_f = htonl(static_cast<uint32_t>(t1 &0xFFFFFFFF));
		packet.txTm_s = tx_s;
		packet.txTm_f = tx_f;

		// Send request
		if (sendto(socket_, (char *)&packet, sizeof(packet),0,
			 (struct sockaddr *)&serverAddr_,
			 sizeof(serverAddr_)) == SOCKET_ERROR) {
			std::cerr << "Send failed\n";
			return false;
		}

		// Receive response
		int addrLen = sizeof(serverAddr_);
		int bytesReceived = recvfrom(socket_, (char *)&packet, sizeof(packet),0,
				 (struct sockaddr *)&serverAddr_, &addrLen);

		// T4: Client receive timestamp (destination)
		uint64_t t4 =
			getSystemNs(std::chrono::system_clock::now());

		if (bytesReceived == SOCKET_ERROR || bytesReceived < (int)sizeof(packet)) {
			std::cerr << "Receive failed\n";
			return false;
		}

		// T2: Server receive timestamp
		uint64_t t2 = ntpToUint64(ntohl(packet.rxTm_s), ntohl(packet.rxTm_f));

		// T3: Server transmit timestamp
		uint64_t t3 = ntpToUint64(ntohl(packet.txTm_s), ntohl(packet.txTm_f));

		// Compute NTP algorithm values
		double d1 = ntpDiff(t2, t1);
		double d2 = ntpDiff(t3, t4);
		double d3 = ntpDiff(t4, t1);
		double d4 = ntpDiff(t3, t2);

		/*
		std::cout << "Debug NTP timings: " << std::endl;
		std::cout << "t1 = " << t1 << std::endl;
		std::cout << "t2 = " << t2 << std::endl;
		std::cout << "t3 = " << t3 << std::endl;
		std::cout << "t4 = " << t4 << std::endl;
		std::cout << "d1 = " << d1 << " s" << std::endl;
		std::cout << "d2 = " << d2 << " s" << std::endl;
		*/

		offset = (d1 + d2) /2.0;
		roundTripDelay = d3 - d4;

		return true;
	}

	// Sample the NTP server multiple times and pick the sample with minimum RTT.
	// Additionally compute a scale factor (slope) between system ns and precise ns.
	// Returns true and fills outOffset, outPreciseNs, outDelay, outScale on success.
	bool sampleBest(int samples, double &outOffset, uint64_t &outPreciseNs,
		double &outDelay,
		uint64_t &outSysTime,
		int sleepMs =100)
	{
		double bestDelay = std::numeric_limits<double>::infinity();
		uint64_t bestPrecise = 0;
		uint64_t bestSysTime =
			getSystemNs(std::chrono::system_clock::now());
		double bestOffset = 0.0;

		std::vector<uint64_t> sysSamples;
		std::vector<uint64_t> preciseSamples;

		for (int i =0; i < samples; ++i) {
			double offsetSec =0.0;
			double rtt =0.0;
			if (!getNetworkTime(offsetSec, rtt)) {
				std::this_thread::sleep_for(
					std::chrono::milliseconds(sleepMs));
				continue;
			}

			auto sysNs =
				getSystemNs(
				std::chrono::system_clock::now());
			
			double offsetNs = offsetSec * 1e9;
			uint64_t preciseNsLL = static_cast<uint64_t>(sysNs) + static_cast<int64_t>(std::llround(offsetNs));
			if (preciseNsLL <0) preciseNsLL =0;
			uint64_t preciseNs = static_cast<uint64_t>(preciseNsLL);

			sysSamples.push_back(sysNs);
			preciseSamples.push_back(preciseNs);

			if (rtt < bestDelay) {
				bestDelay = rtt;
				bestOffset = offsetSec;
				bestPrecise = preciseNs;
				bestSysTime = sysNs;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
		}

		if (bestDelay == std::numeric_limits<double>::infinity()) return false;

		outOffset = bestOffset;
		outPreciseNs = bestPrecise;
		outDelay = bestDelay;
		outSysTime = bestSysTime;
		return true;
	}

	void syncSystemToNTP(uint64_t diff, uint64_t systemTime, uint64_t ntpTime) {
		std::lock_guard<std::mutex> lk(baseMutex);
		if (diff < bestDiff) {
			std::cout << "Updating NTP base sync, diff = " << diff
				  << " ns\n";
			bestDiff = diff;
			baseSystemTimeNs = systemTime;
			basePreciseNs = ntpTime;
		}
	}

	uint64_t calcNTPTimeAtSystemTime(uint64_t sysTime) {
		std::lock_guard<std::mutex> lk(baseMutex);
		return sysTime + (basePreciseNs - baseSystemTimeNs);
	}

	uint64_t getSystemNs(std::chrono::system_clock::time_point sysTime)
	{
		return static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
			sysTime.time_since_epoch()).count());
	}

private:
	std::string server_;
	int port_;
	SOCKET socket_;
	struct sockaddr_in serverAddr_;
    uint64_t basePreciseNs; // nanoseconds since Unix epoch at sync
    uint64_t baseSystemTimeNs;
    uint64_t bestDiff;
    std::mutex baseMutex;

	uint64_t ntpToUint64(uint32_t seconds, uint32_t fraction)
	{
		return ((uint64_t)seconds <<32) | fraction;
	}

	double ntpDiff(uint64_t a, uint64_t b)
	{
		int64_t diff = (int64_t)a - (int64_t)b;
		return diff /
		4294967296.0; // Convert from NTP fraction to seconds
	}
};

// Cached estimate: call NTP server once, then estimate current time by applying elapsed system time and scale
NTPCLIENT_API uint64_t getAccurateNetworkTime(const std::string &ntpServer,
						int port)
{
	static bool initialized = false;
	static std::atomic<bool> keepRunning(false);
	static std::thread updaterThread;
	static bool updaterStarted = false;
	static NTPClient ntpClient(ntpServer,port);
	static std::chrono::system_clock::time_point lastQueryTime;
	static uint64_t lastQueryNs = 0;
	static uint64_t lastThisNs = 0;

	if (initialized) {
		return ntpClient.calcNTPTimeAtSystemTime(
			ntpClient.getSystemNs(std::chrono::system_clock::now()));
	}

	if (!ntpClient.initialize()) {
		std::cerr << "Failed to initialize NTP client\n";
		return UINT64_MAX;
	}

	// Take multiple samples and pick the one with minimum round-trip delay
	const int samples =5;
	double bestOffset =0.0;
	double bestDelay = std::numeric_limits<double>::infinity();
	uint64_t bestPreciseNs =0;
	double bestScale =1.0;
	uint64_t bestSysTime;

	double outScale =1.0;
	if (!ntpClient.sampleBest(samples, bestOffset, bestPreciseNs, bestDelay,
			 bestSysTime)) {
		std::cerr << "Failed to obtain any NTP samples\n";
		return UINT64_MAX;
	}

	// Log initial offset and scale
	std::cout << "NTP initial sync: offset=" << std::fixed
		  << std::setprecision(9) << bestOffset << ", delay =" << std::setprecision(6) << bestDelay << " s " << std::endl;
	ntpClient.syncSystemToNTP(1000000001ULL, bestSysTime,
		bestPreciseNs); // First time is best guess at 1 second diff 
	initialized = true;
	
	// Start updater thread once
	if (!updaterStarted) {
		keepRunning = true;
		updaterStarted = true;
		updaterThread = std::thread([ntpServer, port]() {
			while (keepRunning.load()) {
				std::this_thread::sleep_for(std::chrono::seconds(15));
				if (!keepRunning.load()) break;
				// Re-query to update basePreciseNs and scale
				NTPClient c(ntpServer, port);
				if (!c.initialize()) continue;

				uint64_t newPrecise = 0;
				uint64_t newSysTime = 0;
				double newOffset = 0;
				double newDelay = 0.0;
				if (!c.sampleBest(5, newOffset, newPrecise, newDelay, newSysTime)) continue;

				// Compare newPrecise with the calculated NTP time at newSysTime
				uint64_t expectedPrecise =
					ntpClient.calcNTPTimeAtSystemTime(
						newSysTime);

				uint64_t diff =
					(newPrecise > expectedPrecise)
						? (newPrecise - expectedPrecise)
						: (expectedPrecise -
						   newPrecise);

				// update shared base times if this is better
				ntpClient.syncSystemToNTP(diff, newSysTime,
								newPrecise);
				
			}
		});

		// Setup cleanup to stop thread at program exit
		static struct UpdaterCleaner {
			std::thread *thr;
			std::atomic<bool> *flag;
			~UpdaterCleaner() {
				if (flag) flag->store(false);
				if (thr && thr->joinable()) thr->join();
			}
		} cleaner{&updaterThread, &keepRunning};
	}

	return bestPreciseNs;
}

