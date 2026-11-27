#include "giga_edge/modules/audio_io/speaker.h"

#include <stdexcept>
#include <string>

#if defined(__linux__) && !defined(__ANDROID__)

#include "child_pipe.h"

#include "giga_edge/lib/common/non_copyable.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace giga_edge {
namespace {

class AplaySpeaker : public ISpeaker, private NonCopyable {
public:
  AplaySpeaker(int sample_rate_hz, int channels, std::string playback_device);
  ~AplaySpeaker() override;

  void Play(std::vector<float> audio) override;
  void Stop() override;
  bool IsPlaying() const override;
  void SetPlaybackCallback(PlaybackCallback cb) override;

private:
  void Worker();
  void WriteChunk(const std::vector<float>& audio);
  void ClosePipe();

  const std::vector<std::string> command_;

  ChildPipe aplay_;

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::vector<float>> queue_;
  bool busy_ = false;
  std::atomic<bool> running_{true};
  PlaybackCallback callback_;
  std::thread worker_;
};

AplaySpeaker::AplaySpeaker(int sample_rate_hz, int channels, std::string playback_device)
  : command_{
      "aplay",
      "-q",
      "-D",
      std::move(playback_device),
      "-t",
      "raw",
      "-f",
      "S16_LE",
      "-c",
      std::to_string(channels),
      "-r",
      std::to_string(sample_rate_hz)
    } {
  if (sample_rate_hz <= 0 || channels <= 0) {
    throw std::runtime_error("AplaySpeaker: sample rate and channels must be positive");
  }
  worker_ = std::thread([this]() {
    Worker();
  });
}

AplaySpeaker::~AplaySpeaker() {
  running_.store(false);
  cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void AplaySpeaker::Play(std::vector<float> audio) {
  if (audio.empty()) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(std::move(audio));
  }
  cv_.notify_one();
}

void AplaySpeaker::Stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.clear();
}

bool AplaySpeaker::IsPlaying() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return busy_ || !queue_.empty() || aplay_.stream != nullptr;
}

void AplaySpeaker::SetPlaybackCallback(PlaybackCallback cb) {
  std::lock_guard<std::mutex> lock(mutex_);
  callback_ = std::move(cb);
}

void AplaySpeaker::Worker() {
  using namespace std::chrono_literals;
  constexpr auto idle_before_close = 700ms;

  // A write to an aplay that has exited must fail with EPIPE instead of killing the process with SIGPIPE.
  sigset_t sigpipe;
  sigemptyset(&sigpipe);
  sigaddset(&sigpipe, SIGPIPE);
  pthread_sigmask(SIG_BLOCK, &sigpipe, nullptr);

  while (true) {
    std::vector<float> audio;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      const bool has_data = cv_.wait_for(lock, idle_before_close, [this]() {
        return !queue_.empty() || !running_.load();
      });
      if (!has_data || queue_.empty()) {
        const bool stop = !running_.load();
        lock.unlock();
        ClosePipe();
        if (stop) {
          return;
        }
        continue;
      }
      audio = std::move(queue_.front());
      queue_.pop_front();
      busy_ = true;
    }

    WriteChunk(audio);

    PlaybackCallback cb;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      busy_ = false;
      cb = callback_;
    }
    if (cb) {
      cb(audio.size());
    }
  }
}

// Close only once the data has run out: an aplay per chunk produces an audible gap.

void AplaySpeaker::ClosePipe() {
  ChildPipe aplay;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    std::swap(aplay, aplay_);
  }
  CloseChildPipe(aplay);
}

void AplaySpeaker::WriteChunk(const std::vector<float>& audio) {
  FILE* pipe = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!aplay_.stream) {
      aplay_ = OpenChildPipe(command_, false);
      if (!aplay_.stream) {
        std::fprintf(stderr, "AplaySpeaker: failed to start aplay: %s\n", std::strerror(errno));
        return;
      }
    }
    pipe = aplay_.stream;
  }

  constexpr size_t batch_samples = 4096;
  std::vector<int16_t> raw;
  raw.reserve(std::min(audio.size(), batch_samples));

  for (size_t offset = 0; offset < audio.size(); offset += batch_samples) {
    const size_t count = std::min(batch_samples, audio.size() - offset);
    raw.clear();
    for (size_t i = 0; i < count; ++i) {
      const float clamped = std::max(-1.0f, std::min(1.0f, audio[offset + i]));
      raw.push_back(static_cast<int16_t>(std::lround(clamped * 32767.0f)));
    }
    if (std::fwrite(raw.data(), sizeof(int16_t), raw.size(), pipe) != raw.size()) {
      std::fprintf(stderr, "AplaySpeaker: write to aplay failed: %s\n", std::strerror(errno));
      ClosePipe(); // the next chunk starts a new aplay
      return;
    }
  }
  std::fflush(pipe);
}

} // namespace

std::shared_ptr<ISpeaker> CreateAplaySpeaker(int sample_rate_hz, int channels, std::string playback_device) {
  return std::make_shared<AplaySpeaker>(sample_rate_hz, channels, std::move(playback_device));
}

} // namespace giga_edge

#else // !linux

namespace giga_edge {

std::shared_ptr<ISpeaker> CreateAplaySpeaker(int /*sample_rate_hz*/, int /*channels*/, std::string /*device*/) {
  throw std::runtime_error("AplaySpeaker is only implemented on Linux");
}

} // namespace giga_edge

#endif
