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
		uint64_t t1 = getCurrentNTPTimestamp();

		// embed t1 transmit timestamp into packet (network order)
		uint32_t tx_s = htonl(static_cast<uint32_t>(t1 >>32));
		uint32_t tx_f = htonl(static_cast<uint32_t>(t1 &0xFFFFFFFF));
		packet.txTm_s = tx_s;
		packet.txTm_f = tx_f;

		// Send request
		if (sendto(socket_, (char *)&packet, sizeof(packet),0,
			 (struct sockaddr *)&serverAddr_, sizeof(serverAddr_)) == SOCKET_ERROR) {
			std::cerr << "Send failed\n";
			return false;
		}

		// Receive response
		int addrLen = sizeof(serverAddr_);
		int bytesReceived = recvfrom(socket_, (char *)&packet, sizeof(packet),0,
				 (struct sockaddr *)&serverAddr_, &addrLen);

		// T4: Client receive timestamp (destination)
		uint64_t t4 = getCurrentNTPTimestamp();

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

		offset = (d1 + d2) /2.0;
		roundTripDelay = d3 - d4;

		return true;
	}

	// Sample the NTP server multiple times and pick the sample with minimum RTT.
	// Returns true and fills outOffset, outPreciseNs, outDelay on success.
	bool sampleBest(int samples, double &outOffset, uint64_t &outPreciseNs, double &outDelay, int sleepMs =100)
	{
		double bestDelay = std::numeric_limits<double>::infinity();
		uint64_t bestPrecise =0;
		double bestOffset =0.0;

		for (int i =0; i < samples; ++i) {
			double offsetSec =0.0;
			double rtt =0.0;
			if (!getNetworkTime(offsetSec, rtt)) {
				std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
				continue;
			}

			// capture system time and compute precise ns for this sample
			auto now = std::chrono::system_clock::now();
			auto sysNs = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
			long long preciseNsLL = static_cast<long long>(sysNs) + static_cast<long long>(std::llround(offsetSec *1e9));
			if (preciseNsLL <0) preciseNsLL =0;
			uint64_t preciseNs = static_cast<uint64_t>(preciseNsLL);

			if (rtt < bestDelay) {
				bestDelay = rtt;
				bestOffset = offsetSec;
				bestPrecise = preciseNs;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
		}

		if (bestDelay == std::numeric_limits<double>::infinity()) return false;
		outOffset = bestOffset;
		outPreciseNs = bestPrecise;
		outDelay = bestDelay;
		return true;
	}

private:
	std::string server_;
	int port_;
	SOCKET socket_;
	struct sockaddr_in serverAddr_;

	uint64_t getCurrentNTPTimestamp()
	{
		auto now = std::chrono::system_clock::now();
		auto duration = now.time_since_epoch();
		auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
			duration);
		auto fraction = duration - seconds;

		uint64_t ntpSeconds = static_cast<uint64_t>(seconds.count()) + NTP_UNIX_OFFSET;
		uint64_t ntpFraction = std::chrono::duration_cast<std::chrono::duration<uint64_t, std::ratio<1,0x100000000ULL>>>(fraction).count();

		return (ntpSeconds <<32) | ntpFraction;
	}

	uint64_t ntpToUint64(uint32_t seconds, uint32_t fraction)
	{
		return ((uint64_t)seconds <<32) | fraction;
	}

	double ntpDiff(uint64_t a, uint64_t b)
	{
		int64_t diff = (int64_t)a - (int64_t)b;
		return diff /4294967296.0; // Convert from NTP fraction to seconds
	}
};

// Cached estimate: call NTP server once, then estimate current time by applying elapsed system time
uint64_t getAccurateNetworkTime(const std::string &ntpServer = "pool.ntp.org", int port =123) {
	static bool initialized = false;
	static uint64_t basePreciseNs =0; // nanoseconds since Unix epoch at sync
	static std::chrono::system_clock::time_point baseSystemTime;
	static std::mutex baseMutex;
	static std::atomic<bool> keepRunning(false);
	static std::thread updaterThread;
	static bool updaterStarted = false;

	if (initialized) {
		// estimate from base
		std::lock_guard<std::mutex> lk(baseMutex);
		auto now = std::chrono::system_clock::now();
		auto delta = std::chrono::duration_cast<std::chrono::nanoseconds>(now - baseSystemTime).count();
		return basePreciseNs + static_cast<uint64_t>(delta);
	}

	NTPClient client(ntpServer, port);
	if (!client.initialize()) {
		std::cerr << "Failed to initialize NTP client\n";
		return UINT64_MAX;
	}

	// Take multiple samples and pick the one with minimum round-trip delay
	const int samples =5;
	double bestOffset =0.0;
	double bestDelay = std::numeric_limits<double>::infinity();
	uint64_t bestPreciseNs =0;

	if (!client.sampleBest(samples, bestOffset, bestPreciseNs, bestDelay)) {
		std::cerr << "Failed to obtain any NTP samples\n";
		return UINT64_MAX;
	}

	{
		std::lock_guard<std::mutex> lk(baseMutex);
		basePreciseNs = bestPreciseNs;
		baseSystemTime = std::chrono::system_clock::now();
		initialized = true;
	}

	// Start updater thread once
	if (!updaterStarted) {
		keepRunning = true;
		updaterStarted = true;
		updaterThread = std::thread([ntpServer, port]() {
			while (keepRunning.load()) {
				std::this_thread::sleep_for(std::chrono::minutes(1));
				if (!keepRunning.load()) break;
				// Re-query to update basePreciseNs
				NTPClient c(ntpServer, port);
				if (!c.initialize()) continue;

				double bestOffsetLocal =0.0;
				double bestDelayLocal = std::numeric_limits<double>::infinity();
				uint64_t bestPreciseLocal =0;
				if (!c.sampleBest(5, bestOffsetLocal, bestPreciseLocal, bestDelayLocal)) continue;
				// update shared base
				{
					std::lock_guard<std::mutex> lk(baseMutex);
					basePreciseNs = bestPreciseLocal;
					baseSystemTime = std::chrono::system_clock::now();
				}
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
		} cleaner{ &updaterThread, &keepRunning };
	}

	return basePreciseNs;
}
