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
	std::thread timer_thread;

	OutputType output_type = OutputType::BW;
	AudioType audio_type = AudioType::Zero;
	int video_delay = 0;
	bool setcode = false;

	// Parse command line arguments to find /duration=
	for (int i = 1; i < argc; ++i) {
		if (strncmp(argv[i], "-duration=", 10) == 0) {
			duration = std::atoi(argv[i] + 10);
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
		} else if (strncmp(argv[i], "-audio=", 7) == 0) {
			std::string audio_arg = argv[i] + 7;
			if (audio_arg == "Zero") {
				audio_type = AudioType::Zero;
			} else if (audio_arg == "Peak") {
				audio_type = AudioType::Peak;
			} else if (audio_arg == "Spike") {
				audio_type = AudioType::Spike;
			}
		} else if (strncmp(argv[i], "-delay=", 7) == 0) {
			video_delay = std::atoi(argv[i] + 7);
		} else if (strncmp(argv[i], "-setcode", 8) == 0) {
			setcode = true;
		}
	}

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
		sprintf_s<256>(name, "Sync Test BW (%d)",
			       video_delay);
		NDI_send_create_desc.p_ndi_name = name;
		break;
	}
	NDI_send_create_desc.clock_audio = false;

	char message[256];
	sprintf_s<256>(message, "NDI <- SyncTestSend [%s]",
		       NDI_send_create_desc.p_ndi_name);

	// We create the NDI sender
	NDIlib_send_instance_t pNDI_send =
		NDIlib_send_create(&NDI_send_create_desc);
	if (!pNDI_send)
		return 0;

	const int n_white = 30;
	const int n_black = 60;
	const int frame_rate = 30;
	const int audio_rate = 48000;
	uint64_t frame_time = 1000000000 / frame_rate;	
	const int audio_no_samples = audio_rate / frame_rate;

	// We are going to create a 1920x1080 interlaced frame
	NDIlib_video_frame_v2_t NDI_video_frame;
	NDI_video_frame.frame_rate_N = frame_rate * 1000;
	NDI_video_frame.frame_rate_D = 1000;
	NDI_video_frame.xres = 1920;
	NDI_video_frame.yres = 1080;
	NDI_video_frame.FourCC = NDIlib_FourCC_type_UYVY;
	NDI_video_frame.p_data = (uint8_t *)malloc(1920 * 1080 * 2);
	NDI_video_frame.line_stride_in_bytes = 1920 * 2;

	// Create an audio buffer
	NDIlib_audio_frame_v2_t NDI_audio_frame;
	NDI_audio_frame.sample_rate = audio_rate;
	NDI_audio_frame.no_channels = 2;
	NDI_audio_frame.no_samples = audio_no_samples;
	NDI_audio_frame.p_data = (float *)malloc(sizeof(float) * audio_no_samples * 2);
	NDI_audio_frame.channel_stride_in_bytes = sizeof(float) * audio_no_samples;

	int64_t sine_sample = 0;
	bool last_white = false;
	bool last_sound = false;
	struct timespec ts;
	timespec_get(&ts, TIME_UTC);
	long long nanoseconds =
		(long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
	auto start_time = nanoseconds;
	auto last_sync_time = start_time;

	// We will send 1000 frames of video.
	for (int idx = 0; !exit_loop; idx++) {
		// Determine if the frame should be black or white based on the output type
		bool white = false;
		bool sound = false;

		if (output_type == OutputType::BW) {
			white = ((idx - video_delay) % (n_black + n_white)) < n_white;
			sound = (idx % (n_black + n_white)) < n_white;
		} else if (output_type == OutputType::Black) {
			white = false;
			sound = false;
		} else if (output_type == OutputType::White) {
			white = true;
			sound = true;
		}

		// Because we are clocking to the video it is better to always submit the
		// audio before, although there is very little in it. I'll leave it as an
		// exercises for the reader to work out why.
		NDI_audio_frame.no_samples = audio_no_samples;

		if (!last_sound && sound) {
			sine_sample = 0;
		}
	
		// When sound, fill in a 400hz sine wave or a spike at the beginning of the audio frame
		for (int ch = 0; ch < 2; ch++) {
			// Get the pointer to the start of this channel
			float *p_ch =
				(float *)((uint8_t *)NDI_audio_frame.p_data +
					  ch * NDI_audio_frame
							  .channel_stride_in_bytes);

			
			if ((audio_type == AudioType::Zero) ||
			    (audio_type == AudioType::Peak)) {

				// Generate a 400Hz sine wave
				const float frequency = 400.0f;
				const float sample_rate = (float)audio_rate;
				float sine = audio_type == AudioType::Zero
						     ? 2.0f
						     : 1.0f;
				for (int sample_no = 0;
					 sample_no < NDI_audio_frame.no_samples;
					 sample_no++) {
					float time =
						(sine_sample + sample_no) / sample_rate;
					float sample =
						sin(sine * M_PI * frequency * time);
					if (sample == 0.0f)
						sample =
							1.0E-10f; // Make sure we never have a zero sample value
					p_ch[sample_no] = sound ? sample : 0.0f;
				}
			} else if (audio_type == AudioType::Spike) {
				if (!last_sound && sound) {
					p_ch[0] = 1.0f;
					std::fill_n(p_ch + 1,
						    NDI_audio_frame.no_samples -
							    1,
						    0.0f);

				} else {
					std::fill_n(p_ch,
						    NDI_audio_frame.no_samples,
						    0.0f);
				}
			}

			last_sound = sound;
		}			
		
		last_white = white;

		NDI_audio_frame.timestamp = start_time + (idx * frame_time);
		NDI_audio_frame.timecode = NDIlib_send_timecode_synthesize;
		if (setcode) NDI_audio_frame.timecode = NDI_audio_frame.timestamp / 100;

		sine_sample += NDI_audio_frame.no_samples;

		// Log the audio time and audio frame
		obs_sync_debug_log_audio_time(message,
					      NDI_audio_frame.timestamp,
					      NDI_audio_frame.p_data,
					      NDI_audio_frame.no_samples,
					      NDI_audio_frame.sample_rate);

		NDIlib_send_send_audio_v2(pNDI_send, &NDI_audio_frame);

		// Every 50 frames display a few frames of white
		std::fill_n((uint16_t *)NDI_video_frame.p_data, 1920 * 1080,
			    !white ? (128 | (16 << 8)) : (128 | (235 << 8)));

		NDI_video_frame.timecode = NDIlib_send_timecode_synthesize;
		NDI_video_frame.timestamp = start_time + ((idx - video_delay) * frame_time);
		if (setcode) NDI_video_frame.timecode = NDI_video_frame.timestamp / 100;

		// Check if start of white frame and log the frame time, audio time and diff
		obs_sync_debug_log_video_time(message,
					      NDI_video_frame.timestamp,
					      NDI_video_frame.p_data);
		NDIlib_send_send_video_v2(pNDI_send, &NDI_video_frame);
		//std::this_thread::sleep_for(std::chrono::milliseconds(30));
	}

	timer_thread.join();

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
