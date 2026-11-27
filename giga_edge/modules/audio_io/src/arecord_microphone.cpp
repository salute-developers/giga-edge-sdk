#include "giga_edge/modules/audio_io/microphone.h"

#include <stdexcept>
#include <string>

#if defined(__linux__) && !defined(__ANDROID__)

#include "child_pipe.h"

#include "giga_edge/lib/chunk_iterator/chunk_iterator.h"
#include "giga_edge/lib/common/non_copyable.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace giga_edge {
namespace {

class ArecordMicrophone : public IMicrophone, private NonCopyable {
public:
  ArecordMicrophone(
    int sample_rate_hz, int chunk_duration_ms, std::string capture_device, OnChunk on_chunk, OnFinished on_finished
  );

  ~ArecordMicrophone() override {
    ArecordMicrophone::Stop();
  }
  void Stop() override;

private:
  ChildPipe arecord_;
  std::atomic<bool> running_{false};
  std::thread reader_;
  OnFinished on_finished_;
};

ArecordMicrophone::ArecordMicrophone(
  int sample_rate_hz, int chunk_duration_ms, std::string capture_device, OnChunk on_chunk, OnFinished on_finished
) {
  const size_t chunk_size = static_cast<size_t>(sample_rate_hz) * chunk_duration_ms / 1000;
  if (chunk_size == 0) {
    throw std::runtime_error("ArecordMicrophone: computed zero chunk size");
  }
  arecord_ = OpenChildPipe(
    {"arecord",
     "-q",
     "-D",
     capture_device,
     "-t",
     "raw",
     "-f",
     "S16_LE",
     "-c",
     "1",
     "-r",
     std::to_string(sample_rate_hz)},
    true
  );
  if (!arecord_.stream) {
    throw std::runtime_error(
      "Failed to start arecord for capture device '" + capture_device + "': " + std::strerror(errno)
    );
  }
  on_finished_ = std::move(on_finished);
  running_.store(true);

  try {
    reader_ = std::thread([this, cb = std::move(on_chunk), chunk_size]() {
      ChunkIterator<float> chunker(
        ChunkIteratorParams{static_cast<int32_t>(chunk_size), static_cast<int32_t>(chunk_size)}
      );
      int32_t chunk_id = 0;
      std::vector<int16_t> raw(chunk_size);
      std::vector<float> f;
      bool ended_on_its_own = false;
      while (running_.load()) {
        const size_t got = std::fread(raw.data(), sizeof(int16_t), raw.size(), arecord_.stream);
        if (got == 0) {
          ended_on_its_own = true;
          break;
        }
        f.resize(got);
        for (size_t i = 0; i < got; ++i) {
          f[i] = static_cast<float>(raw[i]) / 32768.0f;
        }
        chunker.Push(f);
        while (chunker.HasNext()) {
          if (cb) {
            cb(chunker.Next(), chunk_id++);
          }
        }
      }
      if (ended_on_its_own && on_finished_) {
        on_finished_();
      }
    });
  } catch (...) {
    running_.store(false);
    CloseChildPipe(arecord_);
    throw;
  }
}

void ArecordMicrophone::Stop() {
  if (!running_.exchange(false)) {
    return;
  }

  if (reader_.joinable()) {
    reader_.join();
  }

  CloseChildPipe(arecord_);
}

} // namespace

std::shared_ptr<IMicrophone> CreateArecordMicrophone(
  int sample_rate_hz,
  int chunk_duration_ms,
  std::string capture_device,
  IMicrophone::OnChunk on_chunk,
  IMicrophone::OnFinished on_finished
) {
  return std::make_shared<ArecordMicrophone>(
    sample_rate_hz, chunk_duration_ms, std::move(capture_device), std::move(on_chunk), std::move(on_finished)
  );
}

} // namespace giga_edge

#else // !linux

namespace giga_edge {

std::shared_ptr<IMicrophone> CreateArecordMicrophone(
  int /*sample_rate_hz*/,
  int /*chunk_duration_ms*/,
  std::string /*capture_device*/,
  IMicrophone::OnChunk /*on_chunk*/,
  IMicrophone::OnFinished /*on_finished*/
) {
  throw std::runtime_error("ArecordMicrophone is only implemented on Linux");
}

} // namespace giga_edge

#endif
