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

#define M_PI 3.14159265358f

#ifdef _WIN32
#ifdef _WIN64
#pragma comment(lib, "Processing.NDI.Lib.x64.lib")
#else // _WIN64
#pragma comment(lib, "Processing.NDI.Lib.x86.lib")
#endif // _WIN64
#endif
static std::map<std::string, std::chrono::steady_clock::time_point> lastLogTime;

// Function to log messages with a cooldown for each message
void obs_sync_debug_log(const char *message, uint64_t timecode,
                        uint64_t timestamp) {

  auto now = std::chrono::steady_clock::now();

  // Check if the number of strings in the map is <= 100
  if (lastLogTime.size() <= 100) {

    // Concatenate message and sourceName
    std::string key = std::string(message);

    auto it = lastLogTime.find(key);
    if (it == lastLogTime.end() ||
        std::chrono::duration_cast<std::chrono::seconds>(now - it->second)
                .count() >= 1) {
      printf("tc=%17lld ts=%17lld: %s\n", timecode, timestamp, key.c_str());
      lastLogTime[key] = now;
    }
  }
}
static std::atomic<bool> exit_loop(false);
static void sigint_handler(int) { exit_loop = true; }
enum class OutputType { Black, White, BW };

int main(int argc, char *argv[]) {
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

  // Parse command line arguments to find /duration=
  for (int i = 1; i < argc; ++i) {
    if (strncmp(argv[i], "-duration=", 10) == 0) {
      duration = std::atoi(argv[i] + 10);
      timer_thread = std::thread([duration]() {
        std::this_thread::sleep_for(std::chrono::seconds(duration));
        exit_loop = true;
      });
    }
    else if (strncmp(argv[i], "-output=", 8) == 0) {
        std::string output_arg = argv[i] + 8;
        if (output_arg == "Black") {
            output_type = OutputType::Black;
        }
        else if (output_arg == "White") {
            output_type = OutputType::White;
        }
        else if (output_arg == "BW") {
            output_type = OutputType::BW;
        }
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
      NDI_send_create_desc.p_ndi_name = "Sync Test BW";
      break;
  }

  // We create the NDI sender
  NDIlib_send_instance_t pNDI_send = NDIlib_send_create(&NDI_send_create_desc);
  if (!pNDI_send)
    return 0;

  // We are going to create a 1920x1080 interlaced frame at 29.97Hz.
  NDIlib_video_frame_v2_t NDI_video_frame;
  NDI_video_frame.frame_rate_N = 30000;
  NDI_video_frame.frame_rate_D = 1001;
  NDI_video_frame.xres = 1920;
  NDI_video_frame.yres = 1080;
  NDI_video_frame.FourCC = NDIlib_FourCC_type_UYVY;
  NDI_video_frame.p_data = (uint8_t *)malloc(1920 * 1080 * 2);
  NDI_video_frame.line_stride_in_bytes = 1920 * 2;

  // Because 48kHz audio actually involves 1601.6 samples per frame, we make a
  // basic sequence that we follow.
  static const int audio_no_samples[] = {1602, 1601, 1602, 1601, 1602};

  // Create an audio buffer
  NDIlib_audio_frame_v2_t NDI_audio_frame;
  NDI_audio_frame.sample_rate = 48000;
  NDI_audio_frame.no_channels = 2;
  NDI_audio_frame.no_samples = 1602; // Will be changed on the fly
  NDI_audio_frame.p_data = (float *)malloc(sizeof(float) * 1602 * 2);
  NDI_audio_frame.channel_stride_in_bytes = sizeof(float) * 1602;

  int64_t sine_sample = 0;
  bool last_black = false;

  // We will send 1000 frames of video.
  for (int idx = 0; !exit_loop; idx++) {
      // Determine if the frame should be black or white based on the output type
      bool black = false;
      if (output_type == OutputType::BW) {
          black = (idx % 50) > 10;
      }
      else if (output_type == OutputType::Black) {
          black = true;
      }
      else if (output_type == OutputType::White) {
          black = false;
      }
    // Because we are clocking to the video it is better to always submit the
    // audio before, although there is very little in it. I'll leave it as an
    // exercises for the reader to work out why.
    NDI_audio_frame.no_samples = audio_no_samples[idx % 5];
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();

    if (last_black && !black)
      sine_sample = 0;

    // When not black, insert noise into the buffer. This is a horrible noise,
    // but its just for illustration. Fill in the buffer with silence. It is
    // likely that you would do something much smarter than this.
    for (int ch = 0; ch < 2; ch++) {
      // Get the pointer to the start of this channel
      float *p_ch = (float *)((uint8_t *)NDI_audio_frame.p_data +
                              ch * NDI_audio_frame.channel_stride_in_bytes);

      // Generate a 400Hz sine wave
      const float frequency = 400.0f;
      const float sample_rate = 48000.0f; // Assuming a sample rate of 48kHz
      for (int sample_no = 0; sample_no < NDI_audio_frame.no_samples;
           sample_no++) {
        float time = (sine_sample + sample_no) / sample_rate;
        float sample = sin(2.0f * M_PI * frequency * time);
        if (sample == 0.0f)
          sample = 0.0001f; // Make sure we never have a zero sample value
        p_ch[sample_no] = black ? 0 : sample;
      }

      last_black = black;
    }

    NDI_audio_frame.timecode = now / 100;
    sine_sample += NDI_audio_frame.no_samples;

    // Submit the audio buffer
    NDIlib_send_send_audio_v2(pNDI_send, &NDI_audio_frame);

    // Every 50 frames display a few frames of while
    std::fill_n((uint16_t *)NDI_video_frame.p_data, 1920 * 1080,
                black ? (128 | (16 << 8)) : (128 | (235 << 8)));

    // We now submit the frame. Note that this call will be clocked so that we
    // end up submitting at exactly 29.97fps.
    NDI_video_frame.timecode = now / 100;

    NDIlib_send_send_video_v2(pNDI_send, &NDI_video_frame);

    // Just display something helpful
    // printf("Frame number %d send.\n", idx);
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
