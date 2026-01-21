#include "pch.h"
#define NTPCLIENT_EXPORTS
#include "NTPClient.h"

// Ensure NTPPacket and NTP_UNIX_OFFSET are defined in header; implementations follow.

NTPClient::NTPClient(const std::string &server, int port)
	: server_(server),
	  port_(port),
	  socket_(INVALID_SOCKET),
	  basePreciseNs(0),
	  baseSystemTimeNs(0),
	  bestDiff(0),
	  initialized_(false)
{
	bestDiff = 10000000000ULL;
	if (!initialize()) {
		std::cerr << "NTPClient initialization failed\n";
	}
}

NTPClient::~NTPClient()
{
	if (socket_ != INVALID_SOCKET) {
		closesocket(socket_);
	}
	WSACleanup();
}

bool NTPClient::initialize()
{
	initialized_ = false;

	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		std::cerr << "WSAStartup failed\n";
		return false;
	}

	socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (socket_ == INVALID_SOCKET) {
		std::cerr << "Socket creation failed\n";
		return false;
	}

	// Set socket timeout
	DWORD timeout = 5000; //5 seconds
	setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
		   sizeof(timeout));

	// Resolve server address
	struct addrinfo hints = {0}, *result = nullptr;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	if (getaddrinfo(server_.c_str(), std::to_string(port_).c_str(), &hints,
			&result) != 0) {
		std::cerr << "Failed to resolve server address\n";
		return false;
	}

	serverAddr_ = *(struct sockaddr_in *)result->ai_addr;
	freeaddrinfo(result);
	initialized_ = true;

	/*
	// Take multiple samples and pick the one with minimum round-trip delay
	double offsetSec = 0.0;
	uint64_t outPreciseNs = 0;
	double outDelay = 0.0;
	uint64_t outSysTime = 0;
	if (sampleBest(5, offsetSec, basePreciseNs, outDelay,
			   outSysTime)) {
		std::cout << "Initial NTP sync successful\n";
	} else {
		std::cerr << "Initial NTP sync failed\n";
	}
	*/

	return true;
}
uint64_t NTPClient::getNetworkTimeNow(uint64_t &rt) {
	NTPPacket packet;
	std::memset(&packet, 0, sizeof(packet));
	rt = 0;

	// Set up NTP request packet
	packet.li_vn_mode = 0x1B; // LI=0, VN=3, Mode=3 (client)

	// T1: Client transmit timestamp (origin)
	uint64_t t1 = getSystemNs(std::chrono::system_clock::now());

	// embed t1 transmit timestamp into packet (network order)
	uint32_t tx_s = htonl(static_cast<uint32_t>(t1 >> 32));
	uint32_t tx_f = htonl(static_cast<uint32_t>(t1 & 0xFFFFFFFF));
	packet.txTm_s = tx_s;
	packet.txTm_f = tx_f;

	// Send request
	if (sendto(socket_, (char *)&packet, sizeof(packet), 0,
		   (struct sockaddr *)&serverAddr_,
		   sizeof(serverAddr_)) == SOCKET_ERROR) {
		std::cerr << "Send failed\n";
		return false;
	}

	// Receive response
	int addrLen = sizeof(serverAddr_);
	int bytesReceived = recvfrom(socket_, (char *)&packet, sizeof(packet),
				     0, (struct sockaddr *)&serverAddr_,
				     &addrLen);

	// T4: Client receive timestamp (destination)
	uint64_t t4 = getSystemNs(std::chrono::system_clock::now());

	if (bytesReceived == SOCKET_ERROR ||
	    bytesReceived < (int)sizeof(packet)) {
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
	/*
	std::cout << "t1 = " << t1 << std::endl;
	std::cout << "t2 = " << t2 << std::endl;
	std::cout << "t3 = " << t3 << std::endl;
	std::cout << "t4 = " << t4 << std::endl;
	*/ 
	rt = d3;
	return t3;
	 // +((d2 / (d1 + d2 + (double)(t3 - t2))) * d3);
}

bool NTPClient::getNetworkOffsetFromChronoNow(double &offset, double &roundTripDelay)
{
	NTPPacket packet;
	std::memset(&packet, 0, sizeof(packet));

	// Set up NTP request packet
	packet.li_vn_mode = 0x1B; // LI=0, VN=3, Mode=3 (client)

	// T1: Client transmit timestamp (origin)
	uint64_t t1 = getSystemNs(std::chrono::system_clock::now());

	// embed t1 transmit timestamp into packet (network order)
	uint32_t tx_s = htonl(static_cast<uint32_t>(t1 >> 32));
	uint32_t tx_f = htonl(static_cast<uint32_t>(t1 & 0xFFFFFFFF));
	packet.txTm_s = tx_s;
	packet.txTm_f = tx_f;

	// Send request
	if (sendto(socket_, (char *)&packet, sizeof(packet), 0,
		   (struct sockaddr *)&serverAddr_,
		   sizeof(serverAddr_)) == SOCKET_ERROR) {
		std::cerr << "Send failed\n";
		return false;
	}

	// Receive response
	int addrLen = sizeof(serverAddr_);
	int bytesReceived = recvfrom(socket_, (char *)&packet, sizeof(packet),
				     0, (struct sockaddr *)&serverAddr_,
				     &addrLen);

	// T4: Client receive timestamp (destination)
	uint64_t t4 = getSystemNs(std::chrono::system_clock::now());

	if (bytesReceived == SOCKET_ERROR ||
	    bytesReceived < (int)sizeof(packet)) {
		std::cerr << "Receive failed\n";
		return false;
	}

	// T2: Server receive timestamp
	uint64_t t2 = ntpToUint64(ntohl(packet.rxTm_s), ntohl(packet.rxTm_f));

	// T3: Server transmit timestamp
	uint64_t t3 = ntpToUint64(ntohl(packet.txTm_s), ntohl(packet.txTm_f));

	// Compute NTP algorithm values
	int64_t d1 = ntpDiff(t2, t1);
	int64_t d2 = ntpDiff(t3, t4);
	int64_t d3 = ntpDiff(t4, t1);
	int64_t d4 = ntpDiff(t3, t2);

	std::cout << "Debug NTP timings: " << std::endl;
	std::cout << "t1 = " << t1 << std::endl;
	std::cout << "t2 = " << t2 << std::endl;
	std::cout << "t3 = " << t3 << std::endl;
	std::cout << "t4 = " << t4 << std::endl;
	std::cout << "d1 = " << d1 << " s" << std::endl;
	std::cout << "d2 = " << d2 << " s" << std::endl;


	offset = (d1 + d2) / 2.0;
	roundTripDelay = d3 - d4;
	std::cout << "rt = " << roundTripDelay << " s" << std::endl;
	return true;
}

bool NTPClient::sampleBest(int samples, double &outOffset,
			   uint64_t &outPreciseNs, double &outDelay,
			   uint64_t &outSysTime, int sleepMs)
{
	double bestDelay = std::numeric_limits<double>::infinity();
	uint64_t bestPrecise = 0;
	uint64_t bestSysTime = getSystemNs(std::chrono::system_clock::now());
	double bestOffset = 0.0;

	std::vector<uint64_t> sysSamples;
	std::vector<uint64_t> preciseSamples;

	for (int i = 0; i < samples; ++i) {
		double offsetSec = 0.0;
		double rtt = 0.0;
		if (!getNetworkOffsetFromChronoNow(offsetSec, rtt)) {
			std::this_thread::sleep_for(
				std::chrono::milliseconds(sleepMs));
			continue;
		}

		auto sysNs = getSystemNs(std::chrono::system_clock::now());

		double offsetNs = offsetSec * 1e9;
		uint64_t preciseNsLL =
			static_cast<uint64_t>(sysNs) +
			static_cast<int64_t>(std::llround(offsetNs));
		if (preciseNsLL < 0)
			preciseNsLL = 0;
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

	if (bestDelay == std::numeric_limits<double>::infinity())
		return false;

	outOffset = bestOffset;
	outPreciseNs = bestPrecise;
	outDelay = bestDelay;
	outSysTime = bestSysTime;
	return true;
}

void NTPClient::syncSystemToNTP(uint64_t diff, uint64_t systemTime,
				uint64_t ntpTime)
{
	std::lock_guard<std::mutex> lk(baseMutex);
	if (diff < bestDiff) {
		std::cout << "Updating NTP base sync, diff = " << diff
			  << " ns\n";
		bestDiff = diff;
		baseSystemTimeNs = systemTime;
		basePreciseNs = ntpTime;
	}
}

uint64_t NTPClient::calcNTPTimeAtSystemTime(uint64_t sysTime)
{
	std::lock_guard<std::mutex> lk(baseMutex);
	return sysTime + (basePreciseNs - baseSystemTimeNs);
}

uint64_t NTPClient::getSystemNs(std::chrono::system_clock::time_point sysTime)
{
	return static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			sysTime.time_since_epoch())
			.count());
}

uint64_t NTPClient::ntpToUint64(uint32_t seconds, uint32_t fraction)
{
	uint64_t nanoseconds =
		((uint64_t)fraction * 1000000000ULL) / 4294967296ULL;
	return ((uint64_t)(seconds - 2208988800ULL) * 1000000000ULL) + nanoseconds;
}

int64_t NTPClient::ntpDiff(uint64_t a, uint64_t b)
{
	int64_t diff = (int64_t)a - (int64_t)b;
	return diff;
}

// Cached estimate: call NTP server once, then estimate current time by applying elapsed system time and scale
NTPCLIENT_API uint64_t getAccurateNetworkTime(const std::string &ntpServer,
					      int port)
{
	static bool initialized = false;
	static std::atomic<bool> keepRunning(false);
	static std::thread updaterThread;
	static bool updaterStarted = false;
	static NTPClient *ntpClient;
	static std::chrono::system_clock::time_point lastQueryTime;
	static uint64_t lastQueryNs = 0;
	static uint64_t lastThisNs = 0;

	if (initialized) {
		return ntpClient->calcNTPTimeAtSystemTime(ntpClient->getSystemNs(
			std::chrono::system_clock::now()));
	}

	if (ntpServer.empty())
		return UINT64_MAX;

	ntpClient = new NTPClient(ntpServer, port);

	if (!ntpClient->isInitialized()) {
		std::cerr << "Failed to initialize NTP client\n";
		return UINT64_MAX;
	}

	// Take multiple samples and pick the one with minimum round-trip delay
	const int samples = 5;
	double bestOffset = 0.0;
	double bestDelay = std::numeric_limits<double>::infinity();
	uint64_t bestPreciseNs = 0;
	double bestScale = 1.0;
	uint64_t bestSysTime;

	double outScale = 1.0;
	if (!ntpClient->sampleBest(samples, bestOffset, bestPreciseNs, bestDelay,
				  bestSysTime)) {
		std::cerr << "Failed to obtain any NTP samples\n";
		return UINT64_MAX;
	}

	// Log initial offset and scale
	std::cout << "NTP initial sync: offset=" << std::fixed
		  << std::setprecision(9) << bestOffset
		  << ", delay =" << std::setprecision(6) << bestDelay << " s "
		  << std::endl;
	ntpClient->syncSystemToNTP(
		1000000001ULL, bestSysTime,
		bestPreciseNs); // First time is best guess at1 second diff
	initialized = true;

	// Start updater thread once
	if (!updaterStarted) {
		keepRunning = true;
		updaterStarted = true;
		NTPClient *c = new NTPClient(ntpServer, port);
		updaterThread = std::thread([ntpServer, port, c]() {
			while (keepRunning.load()) {
				std::this_thread::sleep_for(
					std::chrono::seconds(1));
				if (!keepRunning.load())
					break;
				// Re-query to update basePreciseNs and scale
				if (!c->initialize())
					continue;

				uint64_t newPrecise = 0;
				uint64_t newSysTime = 0;
				double newOffset = 0;
				double newDelay = 0.0;
				if (!c->sampleBest(5, newOffset, newPrecise,
						  newDelay, newSysTime))
					continue;

				// Compare newPrecise with the calculated NTP time at newSysTime
				uint64_t expectedPrecise =
					ntpClient->calcNTPTimeAtSystemTime(
						newSysTime);

				uint64_t diff =
					(newPrecise > expectedPrecise)
						? (newPrecise - expectedPrecise)
						: (expectedPrecise -
						   newPrecise);

				// update shared base times if this is better
				ntpClient->syncSystemToNTP(diff, newSysTime,
							  newPrecise);
			}
		});

		// Setup cleanup to stop thread at program exit
		static struct UpdaterCleaner {
			std::thread *thr;
			std::atomic<bool> *flag;
			~UpdaterCleaner()
			{
				if (flag)
					flag->store(false);
				if (thr && thr->joinable())
					thr->join();
			}
		} cleaner{&updaterThread, &keepRunning};
	}

	return bestPreciseNs;
}

