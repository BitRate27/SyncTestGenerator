#include <Processing.NDI.Lib.h>
#include <cstdio>
#include <chrono>
#include <thread>


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
			printf("Video AT: %10lld WT: %10lld Delta: %5lld, Last: %lld %s\n",
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
			printf("Audio AT: %10lld WT: %10lld Delta: %5lld, Last: %lld %s\n",
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

	// Parse command line arguments
	for (int i = 1; i < argc; ++i) {
		if (strncmp(argv[i], "-source=", 8) == 0) {
			desired_source_name = argv[i] + 8;
		} else if (strcmp(argv[i], "-stamp") == 0) {
			sync_type = SyncType::Stamp;
		}
	}

	// Not required, but "correct" (see the SDK documentation).
	if (!NDIlib_initialize())
		return 0;

	// Create a finder
	NDIlib_find_instance_t pNDI_find = NDIlib_find_create_v2();
	if (!pNDI_find)
		return 0;

	// Wait until there is one source
	uint32_t no_sources = 0;
	uint32_t last_no_sources = 0;
	const NDIlib_source_t* p_sources = NULL;
	do {
		// Wait until the sources on the network have changed
		printf("Looking for sources ...\n");
		NDIlib_find_wait_for_sources(pNDI_find, 1000/* One second */);
		last_no_sources = no_sources;
		p_sources = NDIlib_find_get_current_sources(pNDI_find, &no_sources);
	} while (no_sources > last_no_sources);

	// No sources found?
	if (no_sources == 0) {

		return 0;
	}

	int source_index = 0;

	for (int i = 0; i < no_sources; i++) {
		if (strcmp(p_sources[i].p_ndi_name,desired_source_name) == 0) {
			source_index = i;
			break;
		}
		printf("Found source: %s\n", p_sources[i].p_ndi_name);
	}

	if (strcmp(desired_source_name, "") == 0) {
		printf("No source name provided. Usage: SyncTestReceive -source=\"<name listed above>\"\n");
		return 0;
	}
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
	for (const auto start = high_resolution_clock::now(); high_resolution_clock::now() - start < minutes(5);) {
	
		// Get audio samples
		NDIlib_audio_frame_v2_t audio_frame;
		NDIlib_framesync_capture_audio(pNDI_framesync, &audio_frame,
					       48000, 4, 1600);

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
	}

	// Free the frame-sync
	NDIlib_framesync_destroy(pNDI_framesync);

	// Destroy the receiver
	NDIlib_recv_destroy(pNDI_recv);

	// Not required, but nice
	NDIlib_destroy();

	// Finished
	return 0;
}
