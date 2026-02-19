#pragma once
// Force static library usage of NTPClient and link the library if available
#define NTPCLIENT_STATIC
#pragma comment(lib, "NTPClient.lib")

#include <winsock2.h>
#include <Processing.NDI.Lib.h>
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
#include <json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <windows.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

class NTPClient {
private:
	// NTP packet structure (48 bytes)
	struct NTPPacket {
		uint8_t li_vn_mode;      // Leap indicator, Version, Mode
		uint8_t stratum;         // Stratum level
		uint8_t poll;            // Poll interval
		uint8_t precision;       // Precision
		uint32_t rootDelay;      // Root delay
		uint32_t rootDispersion; // Root dispersion
		uint32_t refId;          // Reference ID
		uint32_t refTm_s;        // Reference time-stamp seconds
		uint32_t refTm_f;        // Reference time-stamp fraction
		uint32_t origTm_s;       // Originate time-stamp seconds
		uint32_t origTm_f;       // Originate time-stamp fraction
		uint32_t rxTm_s;         // Receive time-stamp seconds
		uint32_t rxTm_f;         // Receive time-stamp fraction
		uint32_t txTm_s;         // Transmit time-stamp seconds
		uint32_t txTm_f;         // Transmit time-stamp fraction
	};

	static constexpr uint32_t NTP_TIMESTAMP_DELTA = 2208988800ull;
	static constexpr int NTP_PORT = 123;
	static constexpr int TIMEOUT_SECONDS = 5;

	// Convert network byte order to host byte order
	uint32_t ntohl_custom(uint32_t netlong) { return ntohl(netlong); }

	// Convert NTP timestamp to nanoseconds since Unix epoch
	int64_t ntpTimestampToNanoseconds(uint32_t seconds, uint32_t fraction)
	{
		// Convert NTP seconds to Unix seconds
		int64_t unixSeconds =
			static_cast<int64_t>(seconds) - NTP_TIMESTAMP_DELTA;

		// Convert fraction to nanoseconds
		// NTP fraction is in units of 1/(2^32) seconds
		// To convert to nanoseconds: (fraction * 1e9) / 2^32
		int64_t nanoseconds =
			(static_cast<int64_t>(fraction) * 1000000000LL) >> 32;

		// Total nanoseconds since Unix epoch
		return (unixSeconds * 1000000000LL) + nanoseconds;
	}

public:
	NTPClient()
	{
		// Initialize Winsock
		WSADATA wsaData;
		int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (result != 0) {
			throw std::runtime_error("WSAStartup failed: " +
						 std::to_string(result));
		}
	}

	~NTPClient() { WSACleanup(); }

	// Get NTP time in nanoseconds from specified server
	int64_t getTimeNanoseconds(const std::string &serverDomain)
	{
		SOCKET sockfd = INVALID_SOCKET;

		try {
			// Resolve hostname
			struct addrinfo hints = {0};
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_DGRAM;
			hints.ai_protocol = IPPROTO_UDP;

			struct addrinfo *result = nullptr;
			int status =
				getaddrinfo(serverDomain.c_str(),
					    std::to_string(NTP_PORT).c_str(),
					    &hints, &result);
			if (status != 0) {
				throw std::runtime_error(
					"Failed to resolve hostname: ");
			}

			// Create socket
			sockfd = socket(result->ai_family, result->ai_socktype,
					result->ai_protocol);
			if (sockfd == INVALID_SOCKET) {
				freeaddrinfo(result);
				throw std::runtime_error(
					"Failed to create socket: " +
					std::to_string(WSAGetLastError()));
			}

			// Set receive timeout
			DWORD timeout = TIMEOUT_SECONDS * 1000;
			setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,
				   reinterpret_cast<const char *>(&timeout),
				   sizeof(timeout));

			// Prepare NTP request packet
			NTPPacket packet = {0};
			packet.li_vn_mode =
				0x1B; // LI = 0, VN = 3, Mode = 3 (client)

			// Send NTP request
			int sendResult = sendto(
				sockfd, reinterpret_cast<const char *>(&packet),
				sizeof(packet), 0, result->ai_addr,
				static_cast<int>(result->ai_addrlen));

			freeaddrinfo(result);

			if (sendResult == SOCKET_ERROR) {
				throw std::runtime_error(
					"Failed to send NTP request: " +
					std::to_string(WSAGetLastError()));
			}

			// Receive NTP response
			NTPPacket response = {0};
			int recvResult = recv(
				sockfd, reinterpret_cast<char *>(&response),
				sizeof(response), 0);

			if (recvResult == SOCKET_ERROR) {
				int error = WSAGetLastError();
				if (error == WSAETIMEDOUT) {
					throw std::runtime_error(
						"NTP request timed out");
				}
				throw std::runtime_error(
					"Failed to receive NTP response: " +
					std::to_string(error));
			}

			// Extract transmit timestamp from response
			uint32_t txTm_s = ntohl_custom(response.txTm_s);
			uint32_t txTm_f = ntohl_custom(response.txTm_f);

			// Close socket
			closesocket(sockfd);

			// Convert to nanoseconds
			return ntpTimestampToNanoseconds(txTm_s, txTm_f);

		} catch (...) {
			if (sockfd != INVALID_SOCKET) {
				closesocket(sockfd);
			}
			throw;
		}
	}

	// Helper function to convert nanoseconds to readable time string
	std::string nanosecondsToString(int64_t nanoseconds)
	{
		int64_t seconds = nanoseconds / 1000000000LL;
		int64_t nanos = nanoseconds % 1000000000LL;

		time_t timeValue = static_cast<time_t>(seconds);
		struct tm timeInfo;

		if (gmtime_s(&timeInfo, &timeValue) != 0) {
			return "Invalid time";
		}

		char buffer[100];
		strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S",
			 &timeInfo);

		return std::string(buffer) + "." +
		       std::to_string(nanos).insert(
			       0, 9 - std::to_string(nanos).length(), '0') +
		       " UTC";
	}
};
/*
// Example usage
int main()
{
	try {
		NTPClient client;

		// Get time from pool.ntp.org
		std::string server = "pool.ntp.org";
		std::cout << "Querying NTP server: " << server << std::endl;

		int64_t timeNanoseconds = client.getTimeNanoseconds(server);

		std::cout << "NTP time (nanoseconds): " << timeNanoseconds
			  << std::endl;
		std::cout << "Human readable: "
			  << client.nanosecondsToString(timeNanoseconds)
			  << std::endl;

		// Try other servers
		std::cout << "\n--- Testing with time.google.com ---"
			  << std::endl;
		timeNanoseconds = client.getTimeNanoseconds("time.google.com");
		std::cout << "NTP time (nanoseconds): " << timeNanoseconds
			  << std::endl;
		std::cout << "Human readable: "
			  << client.nanosecondsToString(timeNanoseconds)
			  << std::endl;

	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 1;
	}

	return 0;
}
*/

using json = nlohmann::json;
#define M_PI 3.14159265358f

#ifdef _WIN32
#ifdef _WIN64
#pragma comment(lib, "Processing.NDI.Lib.x64.lib")
#else // _WIN64
#pragma comment(lib, "Processing.NDI.Lib.x86.lib")
#endif // _WIN64
#endif
bool audio_on = false;
int64_t audio_on_time;
bool white_on = false;
int64_t white_on_time;

int64_t obs_sync_white_time(int64_t time, uint8_t *p_data)
{
	uint8_t pixel0 = p_data[0];
	uint8_t pixel1 = p_data[1];
	uint8_t pixel2 = p_data[2];
	uint8_t pixel3 = p_data[3];
	bool white = (((pixel0 == 128) && (pixel1 == 235)) ||
		      ((pixel0 == 255) && (pixel1 == 255)));
	return white ? time : 0;
}
int64_t obs_sync_audio_time(int64_t time, float *p_data, int nsamples,
			    int samplerate)
{
	int64_t return_time = 0;
	int sample = 0;
	while (sample < nsamples) {
		float sample_amp = p_data[sample];
		if (sample_amp != 0.0f) {
			int64_t ns_per_sample = 1000000000 / samplerate;
			return_time = time + sample * ns_per_sample;
			return return_time;
		}
		sample++;
	}
	return return_time;
}

NDIlib_FourCC_video_type_e get_format_enum(int fmt)
{
	switch (fmt) {
	case 1498831189:
		return NDIlib_FourCC_type_UYVY;
	case 1096178005:
		return NDIlib_FourCC_type_UYVA;
	case 1095911234:
		return NDIlib_FourCC_type_BGRA;
	case 3:
		return NDIlib_FourCC_type_RGBA;
	case 4:
		return NDIlib_FourCC_type_RGBX;
	case 5:
		return NDIlib_FourCC_type_BGRX;
	default:
		break;
	}
	return NDIlib_FourCC_type_UYVY;
}

void get_black_and_white_color(int format, uint32_t &white, uint32_t &black)
{
	switch (format) {
	case NDIlib_FourCC_type_UYVY:
		white = (128 | (235 << 8));
		black = (128 | (16 << 8));
		break;
	case NDIlib_FourCC_type_UYVA:
		// Use fully opaque alpha (255) so white appears white when composited
		white = (128 | (235 << 8) | (255u << 24)); // Alpha =255
		black = (128 | (16 << 8) | (255u << 24)); // Alpha =255
		break;
	case NDIlib_FourCC_type_BGRA:
	case NDIlib_FourCC_type_RGBA:
	case NDIlib_FourCC_type_RGBX:
	case NDIlib_FourCC_type_BGRX:
		white = 0xFFFFFFFF; // Blue=255, Green=255, Red=255, Alpha=255
		black = 0x00000000; // Blue=0, Green=0, Red=0, Alpha=0
		break;
	default:
		break;
	}
}
void obs_sync_debug_log_video_time(const char *message, uint64_t timestamp,
				   uint8_t *data)
{

	// If white frame is going from off to on, log the frame time, audio time and diff
	int64_t white_time = obs_sync_white_time(timestamp, data);
	if (!white_on && (white_time > 0)) {
		white_on = true;
		white_on_time = white_time;

		int64_t diff = white_on_time - audio_on_time;

		printf("AT %lld WT %lld: %17lld %s\n",
		       audio_on_time, white_on_time,
		       diff, message);

	} else if (white_on && (white_time == 0)) {
		white_on = false;
	}
}
void obs_sync_debug_log_audio_time(const char *message, uint64_t timestamp,
				   float *data, int no_samples, int sample_rate)
{

	// If audio on, log the frame time
	int64_t audio_time =
		obs_sync_audio_time(timestamp, data, no_samples, sample_rate);
	if (!audio_on && (audio_time > 0)) {
		audio_on = true; // set audio on
		audio_on_time = audio_time;
	} else if (audio_on && (audio_time == 0)) {
		audio_on = false;
	}
}
static std::atomic<bool> exit_loop(false);
static void sigint_handler(int)
{
	exit_loop = true;
}
enum class OutputType { Black, White, BW };
enum class AudioType { Zero, Peak, Spike };

static inline uint64_t util_mul_div64(uint64_t num, uint64_t mul, uint64_t div)
{
#if defined(_MSC_VER) && defined(_M_X64) && (_MSC_VER >= 1920)
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
static uint32_t winver = 0;

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
	return util_mul_div64(current_time.QuadPart, 1000000000,
			      get_clockfreq());
}

int main(int argc, char *argv[])
{
#ifdef _WIN32
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

	// Suppress abort, critical-error-handler, and system-error dialogs
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
		     SEM_NOALIGNMENTFAULTEXCEPT);
#endif
	// Not required, but "correct" (see the SDK documentation).
	if (!NDIlib_initialize()) {
		// Cannot run NDI. Most likely because the CPU is not sufficient (see SDK
		// documentation). you can check this directly with a call to
		// NDIlib_is_supported_CPU()
		printf("Cannot run NDI.");
		return 0;
	}

	// Catch interrupt so that we can shut down gracefully
	signal(SIGINT, sigint_handler);

	// Get the duration from the command line arguments, default to 60 seconds
	int duration = 60;

	std::thread timer_thread = {};
	bool timer_thread_started = false;

	OutputType output_type = OutputType::BW;
	bool setcode = false;
	bool send_no_connection = false;
	std::string config_file;
	uint32_t white_color = (128 | (235 << 8));
	uint32_t black_color = (128 | (16 << 8));
	bool use_ntp = false;

	// Parse command line arguments to find /duration=
	for (int i = 1; i < argc; ++i) {
		if (strncmp(argv[i], "-duration=", 10) == 0) {
			duration = std::atoi(argv[i] + 10);
			timer_thread_started = true;
			timer_thread = std::thread([duration]() {
				std::this_thread::sleep_for(
					std::chrono::seconds(duration));
				exit_loop = true;
			});
		} else if (strncmp(argv[i], "-output=", 8) == 0) {
			std::string output_arg = argv[i] + 8;
			if (output_arg == "Black") {
				output_type = OutputType::Black;
			} else if (output_arg == "White") {
				output_type = OutputType::White;
			} else if (output_arg == "BW") {
				output_type = OutputType::BW;
			}
		} else if (strncmp(argv[i], "-setcode", 8) == 0) {
			setcode = true;
		} else if (strncmp(argv[i], "-sendnoconn", 10) == 0) {
			send_no_connection = true;
		} else if (strncmp(argv[i], "-ntp", 4) == 0) {
			use_ntp = true;
		} else if (strncmp(argv[i], "-config=", 8) == 0) {
			config_file = argv[i] + 8;
		}
	}

	int xres = 1920;
	int yres = 1080;
	int frame_rate_N = 30000;
	int frame_rate_D = 1000;
    int format = NDIlib_FourCC_type_UYVY;


	// If a config file is specified, parse it
    if (!config_file.empty()) {
	    try {
		    std::ifstream file(config_file);
		    if (!file.is_open()) {
			    throw std::runtime_error(
				    "Could not open config file: " +
				    config_file);
		    }

		    json config;
		    file >> config;

		    // Read xres and yres from the JSON file
		    if (config.contains("xres") && config.contains("yres")) {
			    xres = config["xres"].get<int>();
			    yres = config["yres"].get<int>();
		    }
		    if (config.contains("frame_rate_N") &&
			config.contains("frame_rate_D")) {
			    frame_rate_N = config["frame_rate_N"].get<int>();
			    frame_rate_D = config["frame_rate_D"].get<int>();
		    }
		    if (config.contains("format")) {
			    format = config["format"].get<int>();
		    }
	    } catch (const std::exception &e) {
		    std::cerr << "Error reading config file: " << e.what()
			      << std::endl;
		    return 1;
	    }
    }


	get_black_and_white_color(format, white_color, black_color);

	std::string config_name = "DefaultName"; // Fallback name
	if (!config_file.empty()) {
		// Find .cfg extension
		size_t ext_pos = config_file.rfind(".cfg");
		if (ext_pos != std::string::npos) {
			// Find last path separator (handle both Windows and Unix separators)
			size_t sep_pos = config_file.find_last_of("\\/");
			if (sep_pos == std::string::npos) {
				// No path, take from start
				config_name = config_file.substr(0, ext_pos);
			} else {
				// Start after separator
				config_name = config_file.substr(sep_pos +1, ext_pos - (sep_pos +1));
			}
		} else {
			// No .cfg extension found — just extract filename portion after last separator
			size_t sep_pos = config_file.find_last_of("\\/");
			if (sep_pos == std::string::npos)
				config_name = config_file;
			else
				config_name = config_file.substr(sep_pos +1);
		}
	}
	std::cout << "Config name: " << config_name.c_str() << std::endl;

	const int n_white = 30;
	const int n_black = 60;
	const int frame_rate = frame_rate_N / frame_rate_D;
	const int audio_rate = 48000;

	const int audio_no_samples = audio_rate / frame_rate;

	// We are going to create a video frame
	NDIlib_video_frame_v2_t NDI_video_frame;
	NDI_video_frame.frame_rate_N = frame_rate_N;
	NDI_video_frame.frame_rate_D = frame_rate_D;
	NDI_video_frame.xres = xres;
	NDI_video_frame.yres = yres;
	// Determine FourCC and allocate buffer according to format
	NDI_video_frame.FourCC = get_format_enum(format);

	// Compute buffer sizes based on format
	NDIlib_FourCC_video_type_e f = NDI_video_frame.FourCC;
	size_t line_stride =0;
	size_t plane1_size =0;
	size_t alpha_plane_size =0;
	size_t total_size =0;

	if (f == NDIlib_FourCC_type_UYVY) {
		line_stride = xres *2; //2 bytes per pixel (UYVY packed)
		plane1_size = (size_t)xres * (size_t)yres *2;
		total_size = plane1_size;
	} else if (f == NDIlib_FourCC_type_UYVA) {
		// UYVA: first a UYVY plane (2 bytes per pixel), then an alpha plane (1 byte per pixel)
		line_stride = xres *2;
		plane1_size = (size_t)xres * (size_t)yres *2;
		alpha_plane_size = (size_t)xres * (size_t)yres; // one byte per pixel
		total_size = plane1_size + alpha_plane_size;
	} else {
		//32-bit packed formats:4 bytes per pixel
		line_stride = xres *4;
		plane1_size = (size_t)xres * (size_t)yres *4;
		total_size = plane1_size;
	}

	NDI_video_frame.line_stride_in_bytes = (int)line_stride;
	NDI_video_frame.p_data = (uint8_t *)malloc(total_size);
	if (!NDI_video_frame.p_data) {
		std::cerr << "Failed to allocate video buffer of size " << total_size << std::endl;
		return 0;
	}

	// Create an audio buffer
	NDIlib_audio_frame_v2_t NDI_audio_frame;
	NDI_audio_frame.sample_rate = audio_rate;
	NDI_audio_frame.no_channels =2;
	NDI_audio_frame.no_samples = audio_no_samples;
	NDI_audio_frame.p_data = (float *)malloc(sizeof(float) * audio_no_samples *2);
	NDI_audio_frame.channel_stride_in_bytes = sizeof(float) * audio_no_samples;

	int64_t sine_sample =0;
	bool last_white = false;
	bool last_sound = false;
	struct timespec ts;

	timespec_get(&ts, TIME_UTC);
	long long nanoseconds = (long long)ts.tv_sec *1000000000LL + ts.tv_nsec;

	uint64_t frame_time = (uint64_t)(1000000000ULL * frame_rate_D / frame_rate_N);
	// frame_time = 33333000ULL; // Force shorter timestamps for testing

	NTPClient client;

	// Get time from pool.ntp.org
	std::string server = "pool.ntp.org";
	std::cout << "Querying NTP server: " << server << std::endl;

	auto start_time = use_ntp ? client.getTimeNanoseconds(server)
				  : nanoseconds;

	auto last_sync_time = start_time;

	// Print the resolution for debugging
	std::cout << "Video resolution: " << xres << "x" << yres << std::endl;
	std::cout << "Frame rate: " << frame_rate_N << "/" << frame_rate_D << std::endl;
	std::cout << "Frame time (ns): " << frame_time << std::endl;	
	std::cout << "Format: " << format << std::endl;
	std::cout << "Audio no samples: " << audio_no_samples << std::endl;
	std::cout << "Command line parameters:";
	for (int i = 1; i < argc; ++i) {
		std::cout << " " << argv[i];
	}
	std::cout << std::endl;
	NDIlib_send_create_t NDI_send_create_desc;
	switch (output_type) {
	case OutputType::Black:
		NDI_send_create_desc.p_ndi_name = "Sync Test Black";
		break;
	case OutputType::White:
		NDI_send_create_desc.p_ndi_name = "Sync Test White";
		break;
	case OutputType::BW:
	default:
		char name[256];
		sprintf_s<256>(name, "Sync Test (%s)", config_name.c_str());
		NDI_send_create_desc.p_ndi_name = name;
		break;
	}
	NDI_send_create_desc.clock_audio = true;
	NDI_send_create_desc.clock_video = true;

	char message[256];
	sprintf_s<256>(message, "NDI <- SyncTestSend [%s]",
		       NDI_send_create_desc.p_ndi_name);

	// We create the NDI sender
	NDIlib_send_instance_t pNDI_send =
		NDIlib_send_create(&NDI_send_create_desc);
	if (!pNDI_send) {
		std::cout << "Sender creation failed." << std::endl;
		return 0;
	}

	// Track last connection check to avoid calling API too often
	auto last_conn_check = std::chrono::steady_clock::now() - std::chrono::milliseconds(500);
	uint32_t last_conn_count = 0;
	uint32_t conn_count = 0;

	std::cout << "Waiting for connection on "
		  << NDI_send_create_desc.p_ndi_name << "..." << std::endl;

	const uint64_t ns_per_sec = 1000000000ULL;

	// Loop until start_time passes an even second to start
	uint64_t start_second = ((start_time / ns_per_sec) + 1) * ns_per_sec;
	uint64_t end_second = start_second + ns_per_sec;
	last_white = true;
	start_time = start_second;

	std::cout << "      White starts at: " << start_second << " ns"
		  << std::endl;
	std::cout << "Starting send loop at: " << start_time << " ns"
		  << std::endl;
	std::cout << "      Ending white at: " << end_second << " ns"
		  << std::endl;

	// We will send video frames until exit
	for (int idx =0; !exit_loop; idx++) {
		if (!send_no_connection) { 
			// Periodically (every500 ms) check the number of connections. If zero, skip sending.
			auto now_check = std::chrono::steady_clock::now();
			if (now_check - last_conn_check >=
			 std::chrono::milliseconds(500)) {
				last_conn_count =
					NDIlib_send_get_no_connections(
						pNDI_send,10);
				last_conn_check = now_check;
			}
			if (last_conn_count ==0) {
				// No receivers connected - skip sending this iteration to avoid unnecessary work
				if (conn_count >0) {
					std::cout
						<< "No receivers connected, pausing send."
						<< std::endl;
					exit_loop = true;
				}
				std::this_thread::sleep_for(
					std::chrono::milliseconds(10));
				conn_count =0;
				continue;
			}
			if (last_conn_count != conn_count)
				std::cout << "Connections changed: "
					 << last_conn_count
					 << " receivers connected."
					 << std::endl;
			conn_count = last_conn_count;
		}

		// Determine if the frame should be black or white based on the output type
		bool white = false;
		bool sound = false;

		frame_time = (uint64_t)(frame_time / 100000) *
			     100000; // adjust frame time to milliseconds

		uint64_t frame_ns =
			start_time + ((uint64_t)idx * frame_time);

		if (output_type == OutputType::BW) {
			white = (frame_ns >= start_second) && (frame_ns <= end_second);
			/* if (white)
				std::cout << "white: idx=" << idx
					  << ", frame_ns=" << frame_ns << std::endl;
			*/
			// Make audio follow the white interval as well
			sound = white;
		} else if (output_type == OutputType::Black) {
			white = false;
			sound = false;
		} else if (output_type == OutputType::White) {
			white = true;
			sound = true;
		}

		NDI_audio_frame.no_samples = audio_no_samples;

		if (!last_sound && sound) {
			sine_sample = 0;
		}

		if (last_white && !white) {
			start_second += (ns_per_sec * 4);
			end_second = start_second + ns_per_sec;
			//std::cout << "New white" << start_second << " ns"
			//	  << std::endl;	
		}

		// Fill audio
		for (int ch =0; ch <2; ch++) {
			float *p_ch = (float *)((uint8_t *)NDI_audio_frame.p_data + ch * NDI_audio_frame.channel_stride_in_bytes);
			const float frequency =400.0f;
			const float sample_rate_f = (float)audio_rate;
			float sine =2.0f; // amplitude
			for (int sample_no = 0; sample_no < NDI_audio_frame.no_samples; sample_no++) {
				float time = (sine_sample + sample_no) / sample_rate_f;
				float sample = sinf(sine * M_PI * frequency * time);
				if (sample ==0.0f) sample =1.0E-10f;
				p_ch[sample_no] = (sound) ? sample :0.0f;
			}
			last_sound = sound;
		}
		
		NDI_audio_frame.timestamp = frame_ns / 100;
		NDI_audio_frame.timecode = NDIlib_send_timecode_synthesize;
		if (setcode) NDI_audio_frame.timecode = (nanoseconds + (idx * frame_time))/100;
		sine_sample += NDI_audio_frame.no_samples;

		// Log the audio time and audio frame
		obs_sync_debug_log_audio_time(message, NDI_audio_frame.timestamp, NDI_audio_frame.p_data, NDI_audio_frame.no_samples, NDI_audio_frame.sample_rate);

		NDIlib_send_send_audio_v2(pNDI_send, &NDI_audio_frame);

		// Fill video buffer according to format
		if (f == NDIlib_FourCC_type_UYVY) {
			// UYVY packed8-bit: memory layout per2 pixels: U0 Y0 V0 Y1
			uint8_t U = static_cast<uint8_t>((white ? white_color : black_color) &0xFF);
			uint8_t Yv = static_cast<uint8_t>(((white ? white_color : black_color) >>8) &0xFF);
			uint8_t V = U; // neutral chroma
			// fill row by row
			uint8_t *p = (uint8_t *)NDI_video_frame.p_data;
			for (int row =0; row < yres; ++row) {
				uint8_t *rowPtr = p + (size_t)row * line_stride;
				for (int x =0; x < xres; x +=2) {
					size_t idx = (size_t)x *2; //2 bytes per pixel
					rowPtr[idx +0] = U; // U0
					rowPtr[idx +1] = Yv; // Y0
					rowPtr[idx +2] = V; // V0
					rowPtr[idx +3] = Yv; // Y1
				}
			}
		} else if (f == NDIlib_FourCC_type_UYVA) {
			// UYVY plane then alpha plane
			uint8_t v =
				(uint8_t)(white ? (uint8_t)(white_color &
								    0xFFFF)
							: (uint8_t)(black_color &
								    0xFFFF));
			std::fill_n((uint8_t *)NDI_video_frame.p_data,
					(size_t)xres * (size_t)yres, v);
			uint8_t *alpha_ptr =
				NDI_video_frame.p_data + plane1_size;
			uint8_t a = (uint8_t)((white ? white_color
							    : black_color) >>
						    24);
			std::fill_n(alpha_ptr,
					(size_t)xres * (size_t)yres, a);
		} else {
			//32-bit packed formats (BGRA/RGBA/...)
			uint32_t v = (uint32_t)(white ? white_color
							    : black_color);
			std::fill_n((uint32_t *)NDI_video_frame.p_data,
					(size_t)xres * (size_t)yres, v);
		}
		
		NDI_video_frame.timestamp = frame_ns / 100;
		NDI_video_frame.timecode = NDIlib_send_timecode_synthesize;
		if (setcode)
			NDI_video_frame.timecode =
				(nanoseconds + (idx * frame_time)) / 100;

		// Check if start of white frame and log the frame time, audio time and diff
		obs_sync_debug_log_video_time(message, NDI_video_frame.timestamp, NDI_video_frame.p_data);
		NDIlib_send_send_video_v2(pNDI_send, &NDI_video_frame);

		last_white = white;
	}

	if (timer_thread_started) {
		timer_thread.join();
	}

	// Free the video frame
	free((void *)NDI_video_frame.p_data);
	free((void *)NDI_audio_frame.p_data);

	// Destroy the NDI sender
	NDIlib_send_destroy(pNDI_send);

	// Not required, but nice
	NDIlib_destroy();

	// Finished
	return 0;
}
