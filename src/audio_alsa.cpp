#include "deskpet/backends.hpp"
#include <alsa/asoundlib.h>
#include <thread>

namespace deskpet {
class PCM {
protected:
  snd_pcm_t* pcm_ = nullptr;
  Format format_;
  PCM(const std::string& device, Format f, snd_pcm_stream_t direction) : format_(f) {
    if (device.empty()) throw Error("AUDIO_DEVICE_ERROR", "Choose a USB PCM device in the configuration; run doctor first");
    int rc = snd_pcm_open(&pcm_, device.c_str(), direction, SND_PCM_NONBLOCK);
    if (rc < 0) throw Error("AUDIO_DEVICE_ERROR", snd_strerror(rc));
    rc = snd_pcm_set_params(pcm_, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
      f.channels, f.rate, 1, 80000);
    if (rc >= 0 && direction == SND_PCM_STREAM_PLAYBACK) {
      snd_pcm_sw_params_t* p; snd_pcm_sw_params_alloca(&p);
      rc = snd_pcm_sw_params_current(pcm_, p);
      snd_pcm_uframes_t buffer = 0, period = 0;
      if (rc >= 0) rc = snd_pcm_get_params(pcm_, &buffer, &period);
      // Prefill up to 40 ms so ordinary IPC scheduling jitter does not immediately underrun.
      if (rc >= 0) rc = snd_pcm_sw_params_set_start_threshold(pcm_, p, std::min<snd_pcm_uframes_t>(buffer, f.rate / 25));
      if (rc >= 0) rc = snd_pcm_sw_params(pcm_, p);
    }
    if (rc < 0) { snd_pcm_close(pcm_); pcm_ = nullptr; throw Error("AUDIO_FORMAT_ERROR", snd_strerror(rc)); }
  }
  void checkIO(snd_pcm_sframes_t n, const char* code) {
    if (n == -EPIPE) throw Error("AUDIO_DISCONTINUITY", "ALSA overrun/underrun; this stream is invalid and must restart");
    if (n < 0 && n != -EAGAIN && n != -EINTR) throw Error(code, snd_strerror(int(n)));
  }
public:
  virtual ~PCM() { if (pcm_) { snd_pcm_drop(pcm_); snd_pcm_close(pcm_); } }
};
class AlsaInput final : public AudioInput, private PCM {
public:
  explicit AlsaInput(const Config& c) : PCM(c.captureDevice, {16000, 1}, SND_PCM_STREAM_CAPTURE) {}
  Bytes read(const std::atomic_bool& cancel) override {
    Bytes out(640); size_t frames = 0; auto deadline = Clock::now() + std::chrono::seconds(2);
    while (frames < 320) {
      checkCancelled(cancel);
      auto n = snd_pcm_readi(pcm_, out.data() + frames * 2, 320 - frames);
      checkIO(n, "AUDIO_DEVICE_ERROR");
      if (n > 0) frames += size_t(n);
      else { if (Clock::now() > deadline) throw Error("AUDIO_TIMEOUT", "No microphone frames for 2 seconds"); snd_pcm_wait(pcm_, 20); }
    }
    return out;
  }
};
class AlsaOutput final : public AudioOutput, private PCM {
public:
  AlsaOutput(const Config& c, Format f) : PCM(c.playbackDevice, f, SND_PCM_STREAM_PLAYBACK) {}
  void write(const Bytes& data, const std::atomic_bool& cancel) override {
    size_t done = 0, count = data.size() / format_.frameBytes();
    auto deadline = Clock::now() + std::chrono::seconds(3);
    while (done < count) {
      checkCancelled(cancel);
      auto n = snd_pcm_writei(pcm_, data.data() + done * format_.frameBytes(), count - done);
      checkIO(n, "AUDIO_OUTPUT_ERROR");
      if (n > 0) done += size_t(n);
      else { if (Clock::now() > deadline) throw Error("AUDIO_TIMEOUT", "Speaker write timed out"); snd_pcm_wait(pcm_, 20); }
    }
  }
  bool started() const override { return snd_pcm_state(pcm_) == SND_PCM_STATE_RUNNING; }
  void start() override {
    if (snd_pcm_state(pcm_) == SND_PCM_STATE_PREPARED) {
      int rc = snd_pcm_start(pcm_);
      if (rc < 0) throw Error("AUDIO_OUTPUT_ERROR", snd_strerror(rc));
    }
    if (!started()) throw Error("AUDIO_OUTPUT_ERROR", "ALSA playback did not enter RUNNING state");
  }
  void drain(const std::atomic_bool& cancel) override {
    auto deadline = Clock::now() + std::chrono::seconds(3);
    for (;;) {
      checkCancelled(cancel);
      int rc = snd_pcm_drain(pcm_);
      if (rc == 0) return;
      if (rc != -EAGAIN && rc != -EINTR) throw Error("AUDIO_OUTPUT_ERROR", snd_strerror(rc));
      if (Clock::now() > deadline) throw Error("AUDIO_TIMEOUT", "Speaker drain timed out");
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  void drop() noexcept override { if (pcm_) snd_pcm_drop(pcm_); }
};
std::unique_ptr<AudioInput> makeAlsaInput(const Config& c) { return std::make_unique<AlsaInput>(c); }
std::unique_ptr<AudioOutput> makeAlsaOutput(const Config& c, Format f) { return std::make_unique<AlsaOutput>(c, f); }
}
