#include <Processing.NDI.Lib.h>
#include <cstdio>
#include <chrono>
#include <thread>
#include <string>
#include <cstdarg>


#ifdef _WIN32
#ifdef _WIN64
#pragma comment(lib, "Processing.NDI.Lib.x64.lib")
#else // _WIN64
#pragma comment(lib, "Processing.NDI.Lib.x86.lib")
#endif // _WIN64
#endif // _WIN32

bool audio_on = false;
int64_t audio_on_time;
bool white_on = false;
int64_t white_on_time;
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
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) %1000;
	time_t tnow = std::chrono::system_clock::to_time_t(now);
	struct tm local_tm;
#if defined(_WIN32)
	localtime_s(&local_tm, &tnow);
#else
	localtime_r(&tnow, &local_tm);
#endif

	char timebuf[32];
	snprintf(timebuf, sizeof(timebuf), "%02d:%02d:%02d.%03d: ",
		local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec,
		static_cast<int>(ms.count()));

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
int64_t obs_sync_white_time(int64_t time, uint8_t* p_data)
{
	uint8_t pixel0 = p_data[0];
	uint8_t pixel1 = p_data[1];
	bool white = (((pixel0 == 128) && (pixel1 == 235)) || ((pixel0 == 255) && (pixel1 == 255)));
	return white ? time : 0;
}
int64_t obs_sync_audio_time(int64_t time, float* p_data, int nsamples, int samplerate)
{
	int64_t return_time = 0;
	int sample = 0;
	while (sample < nsamples) {
		float sample_amp = p_data[sample];
		if (sample_amp != 0.0f) {
			int64_t ns_per_sample = 1000000000 / samplerate;
			return_time = time + sample * ns_per_sample;
			float sample_amp_prev = 0.0f;
			if (sample > 0)
				sample_amp_prev = p_data[sample - 1];
			return return_time;
		}
		sample++;
	}
	return return_time;
}

static uint64_t last_audio_sync_time = 0;
static uint64_t last_video_sync_time = 0;

void obs_sync_debug_log_video_time(const char* message, uint64_t timestamp, uint8_t* data)
{

	// If white frame is going from off to on, log the frame time, audio time and diff
	int64_t white_time = obs_sync_white_time(timestamp, data);
	if (!white_on && (white_time > 0)) {
		white_on = true;
		white_on_time = white_time;

		int64_t diff = white_on_time - audio_on_time;
		if ((abs(diff) / 1000000) < 80) {
			log_file("Video AT: %10lld WT: %10lld Delta: %5lld, Last: %lld %s\n",
			       audio_on_time / 1000000, white_on_time / 1000000,
			       diff / 1000000,
			       (white_on_time - last_video_sync_time) / 1000000,
			       message);
		}			
		last_video_sync_time = white_on_time;
	}
	else if (white_on && (white_time == 0)) {
		white_on = false;
	}
}
void obs_sync_debug_log_audio_time(const char* message, uint64_t timestamp, float* data, int no_samples,
	int sample_rate)
{

	// If audio on, log the frame time
	int64_t audio_time = obs_sync_audio_time(timestamp, data, no_samples, sample_rate);
	if (!audio_on && (audio_time > 0)) {
		audio_on = true; // set audio on
		audio_on_time = audio_time;

		int64_t diff = white_on_time - audio_on_time;
		if ((abs(diff)/1000000) < 80)
			log_file("Audio AT: %10lld WT: %10lld Delta: %5lld, Last: %lld %s\n",
				audio_on_time / 1000000, white_on_time / 1000000,
				diff / 1000000,
				(audio_on_time - last_audio_sync_time) / 1000000, message);
		last_audio_sync_time = audio_on_time;
	}
	else if (audio_on && (audio_time == 0)) {
		audio_on = false;
	}
}
enum class SyncType { Code, Stamp };



int main(int argc, char* argv[])
{
	// Default source name
	const char* desired_source_name = "";
	SyncType sync_type = SyncType::Code;
	std::string log_path;

	// Parse command line arguments
	for (int i =1; i < argc; ++i) {
		if (strncmp(argv[i], "-source=",8) ==0) {
			desired_source_name = argv[i] +8;
		} else if (strcmp(argv[i], "-stamp") ==0) {
			sync_type = SyncType::Stamp;
		} else if (strncmp(argv[i], "-log=",5) ==0) {
			std::string v = argv[i] + 5;
			if (v.size() >=2 && v.front() == '"' && v.back() == '"')
				v = v.substr(1, v.size() -2);
			log_path = v;
		}
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
			strftime(timestr, sizeof(timestr), "%Y-%m-%d %H-%M-%S", &local_tm);
			std::string filename = std::string("Receiver") + timestr + ".log";

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
			fprintf(stderr, "Failed to open log file: %s\n", log_path.c_str());
		} else {
			//setvbuf(log_fp, nullptr, _IOLBF,0);
			atexit(close_log);
			log_file("SyncTestReceive started. log=%s", log_path.c_str());
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
	const NDIlib_source_t* p_sources = NULL;
	do {
		// Wait until the sources on the network have changed
		NDIlib_find_wait_for_sources(pNDI_find, 1000/* One second */);
		last_no_sources = no_sources;
		p_sources = NDIlib_find_get_current_sources(pNDI_find, &no_sources);
	} while (no_sources > last_no_sources);

	// No sources found?
	if (no_sources == 0) {
		log_file("No sources found!");
		return 0;
	}

	int source_index = 0;

	if (strcmp(desired_source_name, "") == 0) {
		printf("No source name provided. Usage: SyncTestReceive -source=\"<name in () listed above>\"\n");
		return 0;
	}

	for (int i =0; i < no_sources; i++) {
		const char *src_name = p_sources[i].p_ndi_name;
		// Exact match
		if (strcmp(src_name, desired_source_name) ==0) {
			source_index = i;
			break;
		}

		// Also allow matching when the desired name appears enclosed in parentheses
		// inside the published name, e.g. "Sync Test (red)" -> desired_source_name = "red"
		if (desired_source_name && desired_source_name[0] != '\0') {
			std::string pattern = std::string("(") + desired_source_name + ")";
			if (strstr(src_name, pattern.c_str()) != nullptr) {
				source_index = i;
				break;
			}
		}
	}

	if (source_index >= no_sources) {
		log_file("Source '%s' not found among %u sources!", desired_source_name, no_sources);
		return 0;
	}

	log_file("Connecting to source '%s': ndi_name='%s' at URL='%s'",
		desired_source_name,
		 p_sources[source_index].p_ndi_name,
		 p_sources[source_index].p_url_address);

	char message[256];
	sprintf_s<256>(message, "NDI -> SyncTestReceive [%s]", desired_source_name);

	NDIlib_recv_create_v3_t recv_desc;
	recv_desc.color_format = NDIlib_recv_color_format_e_UYVY_BGRA;

	// We now have at least one source, so we create a receiver to look at it.
	NDIlib_recv_instance_t pNDI_recv = NDIlib_recv_create_v3(&recv_desc);
	if (!pNDI_recv)
		return 0;

	// Connect to our sources
	NDIlib_recv_connect(pNDI_recv, p_sources + source_index);

	// We are now going to use a frame-synchronizer to ensure that the audio is dynamically
	// resampled and time-based con
	NDIlib_framesync_instance_t pNDI_framesync = NDIlib_framesync_create(pNDI_recv);

	// Destroy the NDI finder. We needed to have access to the pointers to p_sources[0]
	NDIlib_find_destroy(pNDI_find);

	uint64_t last_timestamp = 0LL;
	// Run for one minute
	using namespace std::chrono;
	steady_clock::time_point last_report_time = steady_clock::now();
	for (const auto start = high_resolution_clock::now(); high_resolution_clock::now() - start < minutes(5);) {
	
		// Get audio samples
		NDIlib_audio_frame_v2_t audio_frame;
		NDIlib_framesync_capture_audio(pNDI_framesync, &audio_frame,
				48000,4,1600);

		// Using a frame-sync we can always get data which is the magic and it will adapt
		// to the frame-rate that it is being called with.
		NDIlib_video_frame_v2_t video_frame;
		NDIlib_framesync_capture_video(pNDI_framesync, &video_frame);

		// Display video here. The reason that the frame-sync does not return a frame until it has
		// received the frame (e.g. it could return a black 1920x1080 image p) is that you are likely to
		// want to default to some video standard (NTSC or PAL) and there would be no way to know what
		// your default image should be from an API level.
		if (video_frame.p_data) {

			int frame_time = 1000000000 / (video_frame.frame_rate_N/video_frame.frame_rate_D);
			if ((sync_type == SyncType::Code
					    ? video_frame.timecode * 100
					    : video_frame.timestamp) >
				last_timestamp + frame_time) {

				obs_sync_debug_log_video_time(
					message,
					sync_type == SyncType::Code
						? video_frame.timecode *
								100
						: video_frame.timestamp,
					video_frame.p_data);

				obs_sync_debug_log_audio_time(
					message,
					sync_type == SyncType::Code
						? audio_frame.timecode *
								100
						: audio_frame.timestamp,
					audio_frame.p_data,
					audio_frame.no_samples,
					audio_frame.sample_rate);

				last_timestamp =
					sync_type == SyncType::Code
						? video_frame.timecode *
								100
						: video_frame.timestamp;

			}
		}

		// Release the video. You could keep the frame if you want and release it later.
		NDIlib_framesync_free_audio(pNDI_framesync, &audio_frame);
		// Release the video. You could keep the frame if you want and release it later.
		NDIlib_framesync_free_video(pNDI_framesync, &video_frame);

		// This is our clock. We are going to run at 30Hz and the frame-sync is smart enough to
		// best adapt the video and audio to match that.
		std::this_thread::sleep_for(milliseconds(10));

		// Periodic status report once per second
		{
			auto now = steady_clock::now();
			if (now - last_report_time >= seconds(1)) {
				// Safely print/ log current frame info
				log_file("%s: video=%dx%d timecode=%lld, audio=no_samples=%d timecode=%lld",
					message,
					video_frame.xres, video_frame.yres,
					(long long)video_frame.timecode,
					audio_frame.no_samples, (long long)audio_frame.timecode);
				last_report_time = now;
			}
		}
	}

	// Free the frame-sync
	NDIlib_framesync_destroy(pNDI_framesync);

	// Destroy the receiver
	NDIlib_recv_destroy(pNDI_recv);

	// Not required, but nice
	NDIlib_destroy();

	close_log();

	// Finished
	return 0;
}
