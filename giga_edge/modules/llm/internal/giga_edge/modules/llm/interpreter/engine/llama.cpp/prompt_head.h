#pragma once

#include "ggml.h"
#include "gguf.h"
#include "llama.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace giga_edge {

// A head over the prompt (capability head): a GGUF without a base model, holding the `capability_head.contract`
// contract and the MLP head.0 / head.3. The features for each layer of the contract: its input at the last prompt
// token and the mean over the last mean_pool_tokens.
class LlamaPromptHead {
public:
  // nullptr and error: the file is not a head or does not fit the model.
  static std::unique_ptr<LlamaPromptHead> Load(FILE* file, int32_t n_embd, int32_t n_layer, std::string& error);
  ~LlamaPromptHead();

  LlamaPromptHead(const LlamaPromptHead&) = delete;
  LlamaPromptHead& operator=(const LlamaPromptHead&) = delete;

  const std::vector<std::string>& Labels() const {
    return labels_;
  }
  size_t PoolTokens() const {
    return slots_.size();
  }
  size_t Layers() const {
    return layer_tensors_.size();
  }

  // The index of the contract layer whose input is this tensor of the llama.cpp graph (the output of the previous
  // layer), otherwise -1.
  int LayerIndex(const char* tensor_name) const;
  void StoreRows(int index, int32_t first_pos, const float* rows, int32_t count);
  // Positions from pos on will be recomputed: their rows are no longer valid.
  void DropFrom(int32_t pos);
  // nullopt: not all rows of the tail have been computed.
  std::optional<std::vector<float>> Predict(int32_t end_pos) const;

private:
  struct Slot {
    int32_t pos = -1;
    uint32_t filled = 0;
    std::vector<float> rows;
  };

  LlamaPromptHead() = default;
  static std::unique_ptr<LlamaPromptHead> Parse(
    std::unique_ptr<LlamaPromptHead> head, int32_t n_embd, int32_t n_layer, std::string& error
  );
  std::vector<float> Forward(const std::vector<float>& features) const;

  ggml_context* tensors_ = nullptr;
  gguf_context* gguf_ = nullptr;
  const ggml_tensor* w0_ = nullptr;
  const ggml_tensor* b0_ = nullptr;
  const ggml_tensor* w1_ = nullptr;
  const ggml_tensor* b1_ = nullptr;
  int32_t n_embd_ = 0;
  std::vector<std::string> layer_tensors_;
  std::vector<std::string> labels_;
  // Rows by the position pos % mean_pool_tokens.
  std::vector<Slot> slots_;
};

} // namespace giga_edge
