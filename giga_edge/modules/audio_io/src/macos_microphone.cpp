#include "giga_edge/modules/audio_io/microphone.h"

#include <stdexcept>
#include <string>

#if defined(__APPLE__)

#include "giga_edge/lib/chunk_iterator/chunk_iterator.h"
#include "giga_edge/lib/common/non_copyable.h"

#include <AudioToolbox/AudioToolbox.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace giga_edge {
namespace {

constexpr int kNumBuffers = 3;

class MacosMicrophone : public IMicrophone, private NonCopyable {
public:
  MacosMicrophone(int sample_rate_hz, int chunk_duration_ms, OnChunk on_chunk);

  ~MacosMicrophone() override {
    MacosMicrophone::Stop();
  }

  void Stop() override;

private:
  static int32_t ValidatedChunkSize(int sample_rate_hz, int chunk_duration_ms);
  static void HandleInputBuffer(
    void* user_data,
    AudioQueueRef queue,
    AudioQueueBufferRef buffer,
    const AudioTimeStamp* /*start_time*/,
    UInt32 /*num_packet_descs*/,
    const AudioStreamPacketDescription* /*pkts*/
  );

  const int32_t chunk_size_;
  AudioQueueRef queue_ = nullptr;
  ChunkIterator<float> chunker_;
  int32_t next_chunk_id_ = 0;
  OnChunk on_chunk_;
  std::atomic<bool> running_{false};
  std::mutex chunker_mutex_;
};

int32_t MacosMicrophone::ValidatedChunkSize(int sample_rate_hz, int chunk_duration_ms) {
  const int64_t chunk_size_64 = static_cast<int64_t>(sample_rate_hz) * chunk_duration_ms / 1000;
  if (chunk_size_64 <= 0 || chunk_size_64 > INT32_MAX) {
    throw std::runtime_error("MacosMicrophone: invalid chunk size");
  }
  return static_cast<int32_t>(chunk_size_64);
}

void MacosMicrophone::HandleInputBuffer(
  void* user_data,
  AudioQueueRef queue,
  AudioQueueBufferRef buffer,
  const AudioTimeStamp* /*start_time*/,
  UInt32 /*num_packet_descs*/,
  const AudioStreamPacketDescription* /*pkts*/
) {
  auto* self = static_cast<MacosMicrophone*>(user_data);
  const float* samples = static_cast<const float*>(buffer->mAudioData);
  UInt32 num_samples = buffer->mAudioDataByteSize / sizeof(float);

  std::vector<std::vector<float>> ready;
  {
    std::lock_guard<std::mutex> lock(self->chunker_mutex_);
    self->chunker_.Push(std::vector<float>(samples, samples + num_samples));
    while (self->chunker_.HasNext()) {
      ready.push_back(self->chunker_.Next());
    }
  }
  for (auto& chunk : ready) {
    if (self->on_chunk_) {
      self->on_chunk_(std::move(chunk), self->next_chunk_id_++);
    }
  }

  if (self->running_.load()) {
    AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
  }
}

MacosMicrophone::MacosMicrophone(int sample_rate_hz, int chunk_duration_ms, OnChunk on_chunk)
  : chunk_size_(ValidatedChunkSize(sample_rate_hz, chunk_duration_ms))
  , chunker_(ChunkIteratorParams{chunk_size_, /*step_size=*/chunk_size_})
  , on_chunk_(std::move(on_chunk)) {
  running_.store(true);

  AudioStreamBasicDescription format = {};
  format.mSampleRate = sample_rate_hz;
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
  format.mFramesPerPacket = 1;
  format.mChannelsPerFrame = 1;
  format.mBitsPerChannel = sizeof(float) * 8;
  format.mBytesPerPacket = sizeof(float);
  format.mBytesPerFrame = sizeof(float);

  OSStatus status = AudioQueueNewInput(
    &format, &MacosMicrophone::HandleInputBuffer, this, nullptr, kCFRunLoopCommonModes, 0, &queue_
  );
  if (status != noErr) {
    throw std::runtime_error("AudioQueueNewInput failed: " + std::to_string(status));
  }

  try {
    const UInt32 buffer_bytes = static_cast<UInt32>(static_cast<size_t>(chunk_size_) * sizeof(float) * 2);
    for (int i = 0; i < kNumBuffers; ++i) {
      AudioQueueBufferRef buf;
      status = AudioQueueAllocateBuffer(queue_, buffer_bytes, &buf);
      if (status != noErr) {
        throw std::runtime_error("AudioQueueAllocateBuffer failed: " + std::to_string(status));
      }
      status = AudioQueueEnqueueBuffer(queue_, buf, 0, nullptr);
      if (status != noErr) {
        throw std::runtime_error("AudioQueueEnqueueBuffer failed: " + std::to_string(status));
      }
    }
    status = AudioQueueStart(queue_, nullptr);
    if (status != noErr) {
      throw std::runtime_error("AudioQueueStart failed: " + std::to_string(status));
    }
  } catch (...) {
    AudioQueueStop(queue_, true);
    AudioQueueDispose(queue_, true);
    queue_ = nullptr;
    throw;
  }
}

void MacosMicrophone::Stop() {
  bool was_running = running_.exchange(false);
  if (was_running && queue_) {
    AudioQueueStop(queue_, true);
    AudioQueueDispose(queue_, true);
    queue_ = nullptr;
  }
}

} // namespace

std::shared_ptr<IMicrophone> CreateMacosMicrophone(
  int sample_rate_hz, int chunk_duration_ms, IMicrophone::OnChunk on_chunk
) {
  return std::make_shared<MacosMicrophone>(sample_rate_hz, chunk_duration_ms, std::move(on_chunk));
}

} // namespace giga_edge

#else // !__APPLE__

namespace giga_edge {

std::shared_ptr<IMicrophone> CreateMacosMicrophone(
  int /*sample_rate_hz*/, int /*chunk_duration_ms*/, IMicrophone::OnChunk /*on_chunk*/
) {
  throw std::runtime_error("MacosMicrophone is only implemented on macOS");
}

} // namespace giga_edge

#endif
