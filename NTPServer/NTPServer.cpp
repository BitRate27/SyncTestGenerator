#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <chrono>
#include <cstdint>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

// NTP packet structure (48 bytes)
struct NTPPacket {
	uint8_t li_vn_mode;         // Leap Indicator (2), Version (3), Mode (3)
	uint8_t stratum;            // Stratum level
	uint8_t poll;               // Poll interval
	uint8_t precision;          // Precision
	uint32_t root_delay;        // Root delay
	uint32_t root_dispersion;   // Root dispersion
	uint32_t ref_id;            // Reference ID
	uint32_t ref_timestamp_sec; // Reference timestamp (seconds)
	uint32_t ref_timestamp_frac;   // Reference timestamp (fraction)
	uint32_t orig_timestamp_sec;   // Origin timestamp (seconds)
	uint32_t orig_timestamp_frac;  // Origin timestamp (fraction)
	uint32_t recv_timestamp_sec;   // Receive timestamp (seconds)
	uint32_t recv_timestamp_frac;  // Receive timestamp (fraction)
	uint32_t trans_timestamp_sec;  // Transmit timestamp (seconds)
	uint32_t trans_timestamp_frac; // Transmit timestamp (fraction)
};

// Convert system time to NTP timestamp
void systemTimeToNTP(const std::chrono::system_clock::time_point &tp,
		     uint32_t &seconds, uint32_t &fraction)
{
	// NTP epoch: January 1, 1900
	// Unix epoch: January 1, 1970
	// Difference: 70 years + 17 leap days = 2,208,988,800 seconds
	const uint64_t NTP_UNIX_OFFSET = 2208988800ULL;

	auto duration = tp.time_since_epoch();
	auto secs = std::chrono::duration_cast<std::chrono::seconds>(duration);
	auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
		duration - secs);

	// Convert to NTP format
	seconds = static_cast<uint32_t>(secs.count() + NTP_UNIX_OFFSET);

	// Convert nanoseconds to NTP fraction (2^32 * nanoseconds / 1,000,000,000)
	fraction = static_cast<uint32_t>((nanos.count() * 4294967296ULL) /
					 1000000000ULL);
}

uint32_t htonl_custom(uint32_t hostlong)
{
	return htonl(hostlong);
}

uint32_t ntohl_custom(uint32_t netlong)
{
	return ntohl(netlong);
}

int main()
{
	WSADATA wsaData;
	SOCKET sockfd;
	struct sockaddr_in server_addr, client_addr;
	int client_len = sizeof(client_addr);

	// Initialize Winsock
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		std::cerr << "WSAStartup failed" << std::endl;
		return 1;
	}

	// Create UDP socket
	sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sockfd == INVALID_SOCKET) {
		std::cerr << "Socket creation failed: " << WSAGetLastError()
			  << std::endl;
		WSACleanup();
		return 1;
	}

	// Configure server address
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(123); // NTP port

	// Bind socket
	if (bind(sockfd, (struct sockaddr *)&server_addr,
		 sizeof(server_addr)) == SOCKET_ERROR) {
		std::cerr << "Bind failed: " << WSAGetLastError() << std::endl;
		std::cerr << "Note: Port 123 requires administrator privileges"
			  << std::endl;
		closesocket(sockfd);
		WSACleanup();
		return 1;
	}

	std::cout << "NTP Server listening on port 123..." << std::endl;
	std::cout << "Press Ctrl+C to stop" << std::endl;

	while (true) {
		NTPPacket request, response;

		// Receive request
		int recv_len =
			recvfrom(sockfd, (char *)&request, sizeof(request), 0,
				 (struct sockaddr *)&client_addr, &client_len);

		if (recv_len == SOCKET_ERROR) {
			std::cerr << "Receive failed: " << WSAGetLastError()
				  << std::endl;
			continue;
		}

		char client_ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip,
			  INET_ADDRSTRLEN);
		std::cout << "Request from " << client_ip << std::endl;

		// Get current time for receive and transmit timestamps
		auto now = std::chrono::system_clock::now();
		uint32_t recv_sec, recv_frac, trans_sec, trans_frac;
		systemTimeToNTP(now, recv_sec, recv_frac);

		// Small delay to differentiate transmit time (optional)
		systemTimeToNTP(std::chrono::system_clock::now(), trans_sec,
				trans_frac);

		// Build response packet
		memset(&response, 0, sizeof(response));

		// LI=0 (no warning), VN=4 (NTPv4), Mode=4 (server)
		response.li_vn_mode = (0 << 6) | (4 << 3) | 4;
		response.stratum = 1; // Primary reference (stratum 1)
		response.poll = 10;   // 2^10 = 1024 seconds
		response.precision = static_cast<int8_t>(-20); // ~1 microsecond

		response.root_delay = 0;
		response.root_dispersion = 0;
		response.ref_id =
			htonl_custom(0x4C4F434B); // "LOCK" - Local Clock

		// Set reference timestamp to current time
		response.ref_timestamp_sec = htonl_custom(trans_sec);
		response.ref_timestamp_frac = htonl_custom(trans_frac);

		// Copy origin timestamp from request's transmit timestamp
		response.orig_timestamp_sec = request.trans_timestamp_sec;
		response.orig_timestamp_frac = request.trans_timestamp_frac;

		// Set receive timestamp
		response.recv_timestamp_sec = htonl_custom(recv_sec);
		response.recv_timestamp_frac = htonl_custom(recv_frac);

		// Set transmit timestamp
		response.trans_timestamp_sec = htonl_custom(trans_sec);
		response.trans_timestamp_frac = htonl_custom(trans_frac);

		// Send response
		if (sendto(sockfd, (char *)&response, sizeof(response), 0,
			   (struct sockaddr *)&client_addr,
			   client_len) == SOCKET_ERROR) {
			std::cerr << "Send failed: " << WSAGetLastError()
				  << std::endl;
		} else {
			std::cout << "Response sent to " << client_ip
				  << std::endl;
		}
	}

	closesocket(sockfd);
	WSACleanup();
	return 0;
}