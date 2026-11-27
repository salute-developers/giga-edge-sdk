#include "giga_edge/modules/audio_io/speaker.h"

#include <stdexcept>
#include <string>

#if defined(__APPLE__)

#include "giga_edge/lib/common/non_copyable.h"

#include <AudioToolbox/AudioToolbox.h>
#include <algorithm>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace giga_edge {
namespace {

constexpr int kNumBuffers = 6;
constexpr int kBufferSamples = 4000; // 0.25 s @ 16 kHz

class MacosSpeaker : public ISpeaker, private NonCopyable {
public:
  MacosSpeaker(int sample_rate_hz, int channels);
  ~MacosSpeaker() override;

  void Play(std::vector<float> audio) override;
  void Stop() override; // keeps the speaker usable
  bool IsPlaying() const override;
  void SetPlaybackCallback(PlaybackCallback cb) override;

private:
  bool TryEnqueueLocked(AudioQueueBufferRef buf);
  void PumpLocked();
  static void HandleOutputBuffer(void* user_data, AudioQueueRef queue, AudioQueueBufferRef buffer);

  AudioQueueRef queue_ = nullptr;
  std::atomic<bool> running_{false};

  mutable std::mutex mu_;
  std::deque<float> ring_;
  std::vector<AudioQueueBufferRef> idle_buffers_;
  int in_flight_ = 0;
  bool started_ = false;

  std::mutex cb_mu_;
  PlaybackCallback playback_callback_;
};

bool MacosSpeaker::TryEnqueueLocked(AudioQueueBufferRef buf) {
  const size_t cap = buf->mAudioDataBytesCapacity / sizeof(float);
  const size_t n = std::min(cap, ring_.size());
  if (n == 0) {
    return false;
  }
  float* dst = static_cast<float*>(buf->mAudioData);
  std::copy(ring_.begin(), ring_.begin() + n, dst);
  buf->mAudioDataByteSize = static_cast<UInt32>(n * sizeof(float));
  if (AudioQueueEnqueueBuffer(queue_, buf, 0, nullptr) != noErr) {
    return false; // ring untouched: retried later
  }
  ring_.erase(ring_.begin(), ring_.begin() + n);
  ++in_flight_;
  return true;
}

void MacosSpeaker::PumpLocked() {
  while (!idle_buffers_.empty() && !ring_.empty()) {
    AudioQueueBufferRef buf = idle_buffers_.back();
    idle_buffers_.pop_back();
    if (!TryEnqueueLocked(buf)) {
      idle_buffers_.push_back(buf);
      break;
    }
  }
  if (in_flight_ > 0 && !started_) {
    AudioQueueStart(queue_, nullptr);
    started_ = true;
  }
}

void MacosSpeaker::HandleOutputBuffer(void* user_data, AudioQueueRef /*queue*/, AudioQueueBufferRef buffer) {
  auto* self = static_cast<MacosSpeaker*>(user_data);
  const size_t samples_played = buffer->mAudioDataByteSize / sizeof(float);
  PlaybackCallback cb_copy;
  {
    std::lock_guard<std::mutex> lock(self->cb_mu_);
    cb_copy = self->playback_callback_;
  }
  if (cb_copy) {
    cb_copy(samples_played);
  }

  std::lock_guard<std::mutex> lock(self->mu_);
  --self->in_flight_;
  const bool refilled = self->running_.load() && !self->ring_.empty() && self->TryEnqueueLocked(buffer);
  if (!refilled) {
    self->idle_buffers_.push_back(buffer);
  }
}

MacosSpeaker::MacosSpeaker(int sample_rate_hz, int channels) {
  AudioStreamBasicDescription format = {};
  format.mSampleRate = sample_rate_hz;
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
  format.mFramesPerPacket = 1;
  format.mChannelsPerFrame = static_cast<UInt32>(channels);
  format.mBitsPerChannel = sizeof(float) * 8;
  format.mBytesPerFrame = sizeof(float) * channels;
  format.mBytesPerPacket = format.mBytesPerFrame;

  OSStatus status = AudioQueueNewOutput(
    &format, &MacosSpeaker::HandleOutputBuffer, this, nullptr, kCFRunLoopCommonModes, 0, &queue_
  );
  if (status != noErr) {
    throw std::runtime_error("AudioQueueNewOutput failed: " + std::to_string(status));
  }
  // From here the queue is live; dispose it if buffer allocation throws, since a
  // partially-constructed object never runs ~MacosSpeaker().
  try {
    const UInt32 buffer_bytes = static_cast<UInt32>(kBufferSamples * channels * sizeof(float));
    for (int i = 0; i < kNumBuffers; ++i) {
      AudioQueueBufferRef buf = nullptr;
      status = AudioQueueAllocateBuffer(queue_, buffer_bytes, &buf);
      if (status != noErr) {
        throw std::runtime_error("AudioQueueAllocateBuffer (output) failed: " + std::to_string(status));
      }
      idle_buffers_.push_back(buf);
    }
  } catch (...) {
    AudioQueueStop(queue_, true);
    AudioQueueDispose(queue_, true);
    queue_ = nullptr;
    throw;
  }
  // Started lazily on the first Play() (PumpLocked), so we never run an empty queue.
  running_.store(true);
}

MacosSpeaker::~MacosSpeaker() {
  running_.store(false);
  if (queue_) {
    AudioQueueStop(queue_, true);
    AudioQueueDispose(queue_, true);
    queue_ = nullptr;
  }
}

void MacosSpeaker::Play(std::vector<float> audio) {
  if (!running_.load() || audio.empty()) {
    return;
  }

  // A fully-drained-but-started queue keeps its sample clock running through the
  // idle gap; enqueuing into it makes CoreAudio rush playback (~3x too fast). So
  // if the queue is idle, stop it now and let PumpLocked restart it fresh. Must
  // run OUTSIDE mu_: AudioQueueStop(true) waits on the audio thread, which takes mu_.
  bool reset_stale = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    reset_stale = started_ && in_flight_ == 0 && ring_.empty();
  }
  if (reset_stale) {
    AudioQueueStop(queue_, true);
    std::lock_guard<std::mutex> lock(mu_);
    started_ = false;
  }

  std::lock_guard<std::mutex> lock(mu_);
  ring_.insert(ring_.end(), audio.begin(), audio.end());
  PumpLocked();
}

void MacosSpeaker::Stop() {
  if (!running_.load() || !queue_) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mu_);
    ring_.clear();
  }
  AudioQueueReset(queue_);
}

bool MacosSpeaker::IsPlaying() const {
  std::lock_guard<std::mutex> lock(mu_);
  return in_flight_ > 0 || !ring_.empty();
}

void MacosSpeaker::SetPlaybackCallback(PlaybackCallback cb) {
  std::lock_guard<std::mutex> lock(cb_mu_);
  playback_callback_ = std::move(cb);
}

} // namespace

std::shared_ptr<ISpeaker> CreateMacosSpeaker(int sample_rate_hz, int channels) {
  return std::make_shared<MacosSpeaker>(sample_rate_hz, channels);
}

} // namespace giga_edge

#else // !__APPLE__

namespace giga_edge {

std::shared_ptr<ISpeaker> CreateMacosSpeaker(int /*sample_rate_hz*/, int /*channels*/) {
  throw std::runtime_error("MacosSpeaker is only implemented on macOS");
}

} // namespace giga_edge

#endif
