#pragma once

#include "giga_edge/modules/enhance/library.h"

#include <deque>
#include <vector>

namespace giga_edge {

template<typename T>
struct ContextDeleter {
  T* sequencer = nullptr;
  void operator()(enhance::Context<float>* p) const noexcept {
    if (!p || !sequencer) {
      return;
    }
    auto* tmp = p;
    sequencer->DestroyContext(&tmp);
  }
};

class ChunkBuffer {
private:
  std::deque<float> buffer_;

  std::vector<float> output_chunk_;
  std::vector<float> input_chunk_;
  std::shared_ptr<enhance::Sequencer> sequencer_;
  uint32_t chunk_size_;
public:
  ChunkBuffer(std::shared_ptr<enhance::Sequencer> sequencer, uint32_t chunk_size)
    : sequencer_(sequencer)
    , chunk_size_(chunk_size) {
    input_chunk_.resize(chunk_size_);
    output_chunk_.reserve(chunk_size_);
  }

  std::vector<float> ProcessOnce(const std::vector<float>& audio, enhance::Context<float>* ctx) {
    buffer_.insert(buffer_.end(), audio.begin(), audio.end());

    std::vector<float> output;
    output.reserve(audio.size());
    while (buffer_.size() >= chunk_size_) {
      for (size_t i = 0; i < chunk_size_; ++i) {
        input_chunk_[i] = buffer_.front();
        buffer_.pop_front();
      }

      output_chunk_.clear();
      sequencer_->ProcessChunk(input_chunk_, output_chunk_, ctx);
      output.insert(output.end(), output_chunk_.begin(), output_chunk_.end());
    }

    return output;
  }

  void Reset() {
    buffer_.clear();
  }

  ~ChunkBuffer() = default;
};
} // namespace giga_edge