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
using json = nlohmann::json;
#define M_PI 3.14159265358f

#ifdef _WIN32
#ifdef _WIN64
#pragma comment(lib, "Processing.NDI.Lib.x64.lib")
#else // _WIN64
#pragma comment(lib, "Processing.NDI.Lib.x86.lib")
#endif // _WIN64
#endif
int64_t getNTPTimeNanoseconds(const char *, int);
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

		printf("AT %14lld WT %14lld: %14lld %s\n",
		       audio_on_time / 1000000, white_on_time / 1000000,
		       diff / 1000000, message);

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
	int video_delay = 0;
	bool setcode = false;
	bool send_no_connection = false;
	std::string config_file;
	uint32_t white_color = (128 | (235 << 8));
	uint32_t black_color = (128 | (16 << 8));

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
		} else if (strncmp(argv[i], "-delay=", 7) == 0) {
			video_delay = std::atoi(argv[i] + 7);
		} else if (strncmp(argv[i], "-setcode", 8) == 0) {
			setcode = true;
		} else if (strncmp(argv[i], "-sendnoconn", 10) == 0) {
			send_no_connection = true;
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
	uint64_t frame_time = 1000000000 / frame_rate;
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
	auto start_time = nanoseconds;
	auto last_sync_time = start_time;

	// Print the resolution for debugging
	std::cout << "Video resolution: " << xres << "x" << yres << std::endl;
	std::cout << "Frame rate: " << frame_rate_N << "/" << frame_rate_D << std::endl;
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

	// force ntp_time to be the next millisecond
	auto ntp_time = getNTPTimeNanoseconds("pool.ntp.org",123);
	// Round up to the next whole second (strictly greater than the current time)
	if (ntp_time >=0) {
		int64_t sec =1000000000LL;
		ntp_time = ((ntp_time / sec) +1) * sec;
	} else {
		// If negative (shouldn't happen), still normalize to next second
		int64_t sec =1000000000LL;
		ntp_time = ((ntp_time / sec) +1) * sec;
	}
	
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

		if (output_type == OutputType::BW) {
			// Compute time offset since ntp_time, applying video_delay (in frames)
			int64_t adj_idx = static_cast<int64_t>(idx) - static_cast<int64_t>(video_delay);
			int64_t offset_ns_signed = adj_idx * static_cast<int64_t>(frame_time);
			uint64_t offset_ns =0;
			if (offset_ns_signed >0)
				offset_ns = static_cast<uint64_t>(offset_ns_signed);
			else
				offset_ns =0;

			//4-second cycle: white for first1 second, then black for3 seconds
			const uint64_t cycle_ns =4000000000ULL; //4 seconds in ns
			const uint64_t white_ns =1000000000ULL; //1 second in ns
			uint64_t pos = offset_ns % cycle_ns;
			white = (pos < white_ns);
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
			sine_sample =0;
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
		if (white && !last_white) {
			// Reset sine sample on white frame transition
			sine_sample =0;
		}
		last_white = white;

		NDI_audio_frame.timestamp = (ntp_time + (idx * frame_time))/100;
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
		
		NDI_video_frame.timestamp =
			(ntp_time + (idx * frame_time)) / 100;
		NDI_video_frame.timecode = NDIlib_send_timecode_synthesize;
		if (setcode)
			NDI_video_frame.timecode =
				(nanoseconds + (idx * frame_time)) / 100;

		// Check if start of white frame and log the frame time, audio time and diff
		obs_sync_debug_log_video_time(message, NDI_video_frame.timestamp, NDI_video_frame.p_data);
		NDIlib_send_send_video_v2(pNDI_send, &NDI_video_frame);
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
