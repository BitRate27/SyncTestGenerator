#include <Processing.NDI.Lib.h>
#include <cstdio>
#include <chrono>
#include <thread>
#include <string>
#include <cstdarg>
using namespace std::chrono;

#ifdef _WIN32
#ifdef _WIN64
#pragma comment(lib, "Processing.NDI.Lib.x64.lib")
#else // _WIN64
#pragma comment(lib, "Processing.NDI.Lib.x86.lib")
#endif // _WIN64
#endif // _WIN32

bool audio_on = false;
int64_t audio_on_time = -1;
int64_t audio_off_time = -1;

bool white_on = false;
int64_t white_on_time = -1;
int64_t white_off_time = -1;

static FILE *log_fp = nullptr;
static void close_log()
{
	if (log_fp) {
		fclose(log_fp);
		log_fp = nullptr;
	}
}
static void log_file(const char *fmt, ...)
{
	if (!log_fp)
		return;

	// Get current local time with milliseconds
	auto now = std::chrono::system_clock::now();
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
			  now.time_since_epoch()) %
		  1000;
	time_t tnow = std::chrono::system_clock::to_time_t(now);
	struct tm local_tm;
#if defined(_WIN32)
	localtime_s(&local_tm, &tnow);
#else
	localtime_r(&tnow, &local_tm);
#endif

	char timebuf[32];
	snprintf(timebuf, sizeof(timebuf),
		 "%02d:%02d:%02d.%03d: ", local_tm.tm_hour, local_tm.tm_min,
		 local_tm.tm_sec, static_cast<int>(ms.count()));

	// Write timestamp prefix
	fputs(timebuf, log_fp);

	// Write the formatted message
	va_list ap;
	va_start(ap, fmt);
	vfprintf(log_fp, fmt, ap);
	va_end(ap);

	// Newline and flush
	fprintf(log_fp, "\n");
	fflush(log_fp);
}

int64_t obs_sync_white_time(int64_t time, uint8_t *p_data)
{
	uint8_t pixel0 = p_data[0];
	uint8_t pixel1 = p_data[1];
	bool white = (((pixel0 == 128) && (pixel1 == 235)) ||
		      ((pixel0 == 255) && (pixel1 == 255)));
	return white ? time : 0;
}

int64_t obs_sync_audio_on_time(int64_t time, float *p_data, int nsamples,
			       int samplerate)
{
	int64_t return_time = -1;
	int64_t sample = 0;
	float last_amp = 0.0f;
	while (sample < nsamples) {
		float sample_amp = p_data[sample];
		if (sample_amp != last_amp) {
			int64_t ns_per_sample = 1000000000 / samplerate;
			return_time = time + (sample * ns_per_sample);
			return return_time;
		}
		sample++;
	}
	return return_time;
}

int64_t obs_sync_audio_off_time(int64_t time, float *p_data, int nsamples,
				int samplerate)
{
	int64_t return_time = -1;
	int64_t sample = 0;
	float last_amp = 0.0f;
	while (sample < nsamples) {
		float sample_amp = p_data[sample];
		if (sample_amp == last_amp) {
			int64_t ns_per_sample = 1000000000 / samplerate;
			return_time = time + (sample * ns_per_sample);
			return return_time;
		}
		sample++;
	}
	return return_time;
}
const int64_t max_offset = 2000000000LL; //2 seconds
int audio_sync_count = 0;
int video_sync_count = 0;

void obs_sync_debug_video_time(uint64_t timestamp, uint8_t *data)
{
	int64_t white_time = obs_sync_white_time(timestamp, data);
	if (!white_on && (white_time > 0)) {
		white_on = true;
		if (white_on_time != -1)
			video_sync_count++;
		white_on_time = white_time;
		//log_file("White on at %lld", white_on_time);
	} else if (white_on && (white_time == 0)) {
		white_off_time = timestamp;
		white_on = false;
		//log_file("White off at %lld", white_off_time);
	} else if (white_on_time == -1)
		white_on_time = 0;
}
void obs_sync_debug_audio_time(uint64_t timestamp, float *data, int no_samples,
			       int sample_rate)
{
	int64_t audio_time = obs_sync_audio_on_time(timestamp, data, no_samples,
						    sample_rate);
	if (!audio_on && (audio_time > 0)) {
		audio_on = true;
		if (audio_on_time != -1)
			audio_sync_count++;
		audio_on_time = audio_time;
		//log_file("Audio on at %lld", audio_on_time);
	} else if (audio_on) {
		audio_time = obs_sync_audio_off_time(timestamp, data,
						     no_samples, sample_rate);
		if (audio_time > 0) {
			audio_off_time = audio_time;
			audio_on = false;
			//log_file("Audio off at %lld", audio_off_time);
		}
	} else if (audio_on_time == -1)
		audio_on_time = 0;
}
void obs_sync_debug_log(const char *message, int64_t timestamp)
{
	if (timestamp >
	    std::max<int64_t>(audio_on_time, white_on_time) + max_offset) {
		if (white_on_time > 0 && audio_on_time > 0 && audio_sync_count > 0  && video_sync_count > 0) {
			int64_t diff = white_on_time - audio_on_time;
			log_file(
				"%s Sync A/V   AT: %15lld AW: %5lld AC: %3d VT: %15lld VW: %5lld VC: %3d Delta: %5lld",
				message, audio_on_time / 1000000,
				audio_off_time > 0
					? (audio_off_time - audio_on_time) /
						  1000000
					: -1,
				audio_sync_count, white_on_time / 1000000,
				white_off_time > 0
					? (white_off_time - white_on_time) /
						  1000000
					: -1,
				video_sync_count, diff / 1000000);
			audio_on_time = 0;
			audio_off_time = 0;
			white_on_time = 0;
			white_off_time = 0;
		}
		if (white_on_time > 0 && video_sync_count > 0) {
			log_file(
				"%s Sync Video AT: --------------- AW: ----- AC: --- VT: %15lld VW: %5lld VC: %3d Delta: -----",
				message, white_on_time / 1000000,
				white_off_time > 0
					? (white_off_time - white_on_time) /
						  1000000
					: -1,
				video_sync_count);
			white_on_time = 0;
			white_off_time = 0;
		}

		if (audio_on_time > 0 && audio_sync_count > 0) {
			log_file(
				"%s Sync Audio AT: %15lld AW: %5lld AC: %3d VT: --------------- VW: ----- VC: --- Delta: -----",
				message, audio_on_time / 1000000,
				audio_off_time > 0
					? (audio_off_time - audio_on_time) /
						  1000000
					: -1,
				audio_sync_count);
			audio_on_time = 0;
			audio_off_time = 0;
		}
	}
}

enum class SyncType { Code, Stamp };

int main(int argc, char *argv[])
{
	// Default source name
	const char *desired_source_name = "";
	SyncType sync_type = SyncType::Code;
	std::string log_path;
	int duration = 30;
	// Parse command line arguments
	for (int i = 1; i < argc; ++i) {
		if (strncmp(argv[i], "-source=", 8) == 0) {
			desired_source_name = argv[i] + 8;
		} else if (strncmp(argv[i], "-stamp", 6) == 0) {
			sync_type = SyncType::Stamp;
		} else if (strncmp(argv[i], "-log=", 5) == 0) {
			std::string v = argv[i] + 5;
			if (v.size() >= 2 && v.front() == '"' &&
			    v.back() == '"')
				v = v.substr(1, v.size() - 2);
			log_path = v;
		} else if (strncmp(argv[i], "-duration=", 10) == 0) {
			duration = std::atoi(argv[i] + 10);
		}
	}

	if (strcmp(desired_source_name, "") == 0) {
		printf("No source name provided. Usage: SyncTestReceive -source=\"<name in () listed above>\"\n");
		return 0;
	}
	// Not required, but "correct" (see the SDK documentation).
	if (!NDIlib_initialize())
		return 0;

	// Open log file if requested
	if (!log_path.empty()) {
		// Append timestamped filename Receiver<datetime>.log to the provided path
		{
			// Create timestamp in local time formatted as YYYY-MM-DD HH-MM-SS
			auto now = std::chrono::system_clock::now();
			time_t tnow = std::chrono::system_clock::to_time_t(now);
			struct tm local_tm;
#if defined(_WIN32)
			localtime_s(&local_tm, &tnow);
#else
			localtime_r(&tnow, &local_tm);
#endif
			char timestr[64];
			strftime(timestr, sizeof(timestr), "%Y-%m-%d %H-%M-%S",
				 &local_tm);
			std::string filename =
				std::string("Receiver-") +
				std::string(desired_source_name) +
				std::string("-") + timestr + ".log";

			// Ensure log_path ends with a path separator before appending filename
			if (!log_path.empty()) {
				char last = log_path.back();
				if (last != '\\' && last != '/')
					log_path.push_back('\\');
			}
			log_path += filename;
		}

		log_fp = fopen(log_path.c_str(), "a");
		if (!log_fp) {
			fprintf(stderr, "Failed to open log file: %s\n",
				log_path.c_str());
		} else {
			//setvbuf(log_fp, nullptr, _IOLBF,0);
			atexit(close_log);
		}
	}

	// Create a finder
	NDIlib_find_instance_t pNDI_find = NDIlib_find_create_v2();
	if (!pNDI_find) {
		log_file("NDI find create failed");
		return 0;
	}

	// Wait until there is one source
	uint32_t no_sources = 0;
	uint32_t last_no_sources = 0;
	const NDIlib_source_t *p_sources = NULL;
	int source_index = -1;
	const auto start = high_resolution_clock::now();
	
	do
	{
		// Wait until the sources on the network have changed
		NDIlib_find_wait_for_sources(pNDI_find, 1000 /* One second */);
		last_no_sources = no_sources;
		p_sources =
			NDIlib_find_get_current_sources(pNDI_find, &no_sources);

		source_index = -1;
		for (int i = 0; i < no_sources; i++) {
			const char *src_name = p_sources[i].p_ndi_name;
			std::string pattern =
				std::string("(") + desired_source_name + ")";
			if (strstr(src_name, pattern.c_str()) != nullptr) {
				source_index = i;
				break;
			}
		}
	}
	while (source_index == -1 && high_resolution_clock::now() - start < seconds(duration));

	if (source_index == -1) {
		log_file("Source '%s' not found among %u sources!",
			 desired_source_name, no_sources);
		return 0;
	}

	log_file("Connecting to source '%s': ndi_name='%s' at URL='%s'",
		 desired_source_name, p_sources[source_index].p_ndi_name,
		 p_sources[source_index].p_url_address);

	char message[256];
	sprintf_s<256>(message, "NDI -> SyncTestReceive [%s]",
		       desired_source_name);

	NDIlib_recv_create_v3_t recv_desc;

	recv_desc.bandwidth = NDIlib_recv_bandwidth_highest;
	recv_desc.p_ndi_recv_name = nullptr;
	recv_desc.source_to_connect_to.p_ndi_name =
		p_sources[source_index].p_ndi_name;
	recv_desc.color_format = NDIlib_recv_color_format_UYVY_BGRA;
	recv_desc.allow_video_fields = true;
	recv_desc.p_ndi_recv_name = message;

	// We now have at least one source, so we create a receiver to look at it.
	NDIlib_recv_instance_t pNDI_recv = NDIlib_recv_create_v3(&recv_desc);
	if (!pNDI_recv)
		return 0;

	// Connect to our sources
	NDIlib_recv_connect(pNDI_recv, p_sources + source_index);

	// Destroy the NDI finder. We needed to have access to the pointers to p_sources[0]
	NDIlib_find_destroy(pNDI_find);
	NDIlib_audio_frame_v3_t audio_frame = {0};
	NDIlib_video_frame_v2_t video_frame = {0};
	uint64_t last_timestamp = 0LL;

	steady_clock::time_point last_report_time = steady_clock::now();
	for (const auto start = high_resolution_clock::now();
	     high_resolution_clock::now() - start < seconds(duration);) {
		NDIlib_frame_type_e frame_received = NDIlib_recv_capture_v3(
			pNDI_recv, &video_frame, &audio_frame, nullptr, 100);

		if (frame_received == NDIlib_frame_type_audio) {
			if (audio_frame.p_data) {
				float *audio_data = reinterpret_cast<float *>(
					audio_frame.p_data);
				int64_t timestamp =
					sync_type == SyncType::Code
						? audio_frame.timecode * 100
						: audio_frame.timestamp * 100;

				obs_sync_debug_audio_time(
					timestamp, audio_data,
					audio_frame.no_samples,
					audio_frame.sample_rate);

				obs_sync_debug_log(message, timestamp);
			}
			NDIlib_recv_free_audio_v3(pNDI_recv, &audio_frame);
		}
		if (frame_received == NDIlib_frame_type_video) {
			if (video_frame.p_data) {
				int64_t timestamp =
					sync_type == SyncType::Code
						? video_frame.timecode * 100
						: video_frame.timestamp * 100;

				obs_sync_debug_video_time(timestamp,
							  video_frame.p_data);

				obs_sync_debug_log(message, timestamp);
			}
			NDIlib_recv_free_video_v2(pNDI_recv, &video_frame);
		}

		// This is our clock. We are going to run at 30Hz and the frame-sync is smart enough to
		// best adapt the video and audio to match that.
		std::this_thread::sleep_for(milliseconds(10));
	}

	// Destroy the receiver
	NDIlib_recv_destroy(pNDI_recv);

	// Not required, but nice
	NDIlib_destroy();

	close_log();

	// Finished
	return 0;
}
