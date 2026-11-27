#include "giga_edge/modules/llm/interpreter/engine/llama.cpp/prompt_head.h"

#include "giga_edge/modules/assistant_core/json.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>

namespace giga_edge {
namespace {

constexpr const char* kContractKey = "capability_head.contract";

bool CanConvert(const ggml_tensor* tensor) {
  return tensor->type == GGML_TYPE_F32 || ggml_get_type_traits(tensor->type)->to_float != nullptr;
}

bool IsMatrix(const ggml_tensor* tensor, int64_t in, int64_t out) {
  return tensor && CanConvert(tensor) && tensor->ne[0] == in && tensor->ne[1] == out && ggml_nrows(tensor) == out;
}

bool IsVector(const ggml_tensor* tensor, int64_t size) {
  return tensor && CanConvert(tensor) && tensor->ne[0] == size && ggml_nrows(tensor) == 1;
}

void RowToFloat(const ggml_tensor* tensor, int64_t row, float* out) {
  const auto* data = static_cast<const char*>(tensor->data) + row * tensor->nb[1];
  if (tensor->type == GGML_TYPE_F32) {
    std::memcpy(out, data, static_cast<size_t>(tensor->ne[0]) * sizeof(float));
    return;
  }
  ggml_get_type_traits(tensor->type)->to_float(data, out, tensor->ne[0]);
}

bool IsStrings(const Json::Value& value, std::initializer_list<const char*> expected) {
  if (!value.isArray() || value.size() != expected.size()) {
    return false;
  }
  Json::ArrayIndex i = 0;
  for (const char* item : expected) {
    if (!value[i].isString() || value[i].asString() != item) {
      return false;
    }
    ++i;
  }
  return true;
}

} // namespace

std::unique_ptr<LlamaPromptHead> LlamaPromptHead::Load(
  FILE* file, int32_t n_embd, int32_t n_layer, std::string& error
) {
  std::unique_ptr<LlamaPromptHead> head(new LlamaPromptHead());
  head->gguf_ = gguf_init_from_file_ptr(file, gguf_init_params{.no_alloc = false, .ctx = &head->tensors_});
  return Parse(std::move(head), n_embd, n_layer, error);
}

std::unique_ptr<LlamaPromptHead> LlamaPromptHead::Parse(
  std::unique_ptr<LlamaPromptHead> head, int32_t n_embd, int32_t n_layer, std::string& error
) {
  if (!head->gguf_) {
    error = "not a GGUF file";
    return nullptr;
  }
  const int64_t key = gguf_find_key(head->gguf_, kContractKey);
  if (key < 0 || gguf_get_kv_type(head->gguf_, key) != GGUF_TYPE_STRING) {
    error = std::string("no ") + kContractKey;
    return nullptr;
  }
  Json::Value contract;
  if (!ParseJson(gguf_get_val_str(head->gguf_, key), contract, error)) {
    error = "contract: " + error;
    return nullptr;
  }

  // A field of the wrong type: jsoncpp throws.
  try {
    const auto& layers = contract["layers"];
    const auto& classes = contract["classes"];
    const auto& mean_tokens = contract["mean_pool_tokens"];
    if (!contract["schema_version"].isInt() || contract["schema_version"].asInt() != 2) {
      error = "contract: schema_version must be 2";
      return nullptr;
    }
    if (!layers.isArray() || layers.empty() || layers.size() > 32) {
      error = "contract: layers must list 1..32 layers";
      return nullptr;
    }
    for (const auto& layer : layers) {
      // The input of layer L is the output of layer L - 1; llama.cpp cuts the output of the last layer down to the
      // rows with logits.
      if (!layer.isInt() || layer.asInt() < 1 || layer.asInt() > n_layer - 1) {
        error = "contract: layer outside 1.." + std::to_string(n_layer - 1);
        return nullptr;
      }
      auto tensor = "l_out-" + std::to_string(layer.asInt() - 1);
      if (head->LayerIndex(tensor.c_str()) >= 0) {
        error = "contract: layers must not repeat";
        return nullptr;
      }
      head->layer_tensors_.push_back(std::move(tensor));
    }
    if (
      !IsStrings(contract["pooling"], {"last_token", "mean_last_tokens"})
      || contract["concatenation_order"].asString() != "layer_then_pooling"
    ) {
      error = "contract: only last_token + mean_last_tokens, layer_then_pooling";
      return nullptr;
    }
    if (!mean_tokens.isInt() || mean_tokens.asInt() < 1) {
      error = "contract: mean_pool_tokens must be positive";
      return nullptr;
    }
    if (!classes.isArray() || classes.empty()) {
      error = "contract: no classes";
      return nullptr;
    }
    for (const auto& label : classes) {
      if (!label.isString()) {
        error = "contract: class names must be strings";
        return nullptr;
      }
      head->labels_.push_back(label.asString());
    }

    const int64_t input = static_cast<int64_t>(layers.size()) * 2 * n_embd;
    const int64_t hidden = contract["head"]["hidden_size"].asInt64();
    const int64_t output = static_cast<int64_t>(classes.size());
    if (contract["input_size"].asInt64() != input || contract["head"]["output_size"].asInt64() != output) {
      error = "contract: input_size or output_size does not match the model";
      return nullptr;
    }
    head->w0_ = ggml_get_tensor(head->tensors_, "head.0.weight");
    head->b0_ = ggml_get_tensor(head->tensors_, "head.0.bias");
    head->w1_ = ggml_get_tensor(head->tensors_, "head.3.weight");
    head->b1_ = ggml_get_tensor(head->tensors_, "head.3.bias");
    if (
      !IsMatrix(head->w0_, input, hidden) || !IsVector(head->b0_, hidden) || !IsMatrix(head->w1_, hidden, output)
      || !IsVector(head->b1_, output)
    ) {
      error = "head tensors do not match the contract";
      return nullptr;
    }

    head->n_embd_ = n_embd;
    head->slots_.resize(static_cast<size_t>(mean_tokens.asInt()));
    for (auto& slot : head->slots_) {
      slot.rows.resize(layers.size() * static_cast<size_t>(n_embd));
    }
    return head;
  } catch (const Json::Exception& exception) {
    error = std::string("contract: ") + exception.what();
    return nullptr;
  }
}

LlamaPromptHead::~LlamaPromptHead() {
  if (gguf_) {
    gguf_free(gguf_);
  }
  if (tensors_) {
    ggml_free(tensors_);
  }
}

int LlamaPromptHead::LayerIndex(const char* tensor_name) const {
  for (size_t i = 0; i < layer_tensors_.size(); ++i) {
    if (layer_tensors_[i] == tensor_name) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void LlamaPromptHead::StoreRows(int index, int32_t first_pos, const float* rows, int32_t count) {
  for (int32_t i = 0; i < count; ++i) {
    const int32_t pos = first_pos + i;
    auto& slot = slots_[static_cast<size_t>(pos) % slots_.size()];
    if (slot.pos != pos) {
      slot.pos = pos;
      slot.filled = 0;
    }
    std::copy(rows + i * n_embd_, rows + (i + 1) * n_embd_, slot.rows.begin() + index * n_embd_);
    slot.filled |= 1u << index;
  }
}

void LlamaPromptHead::DropFrom(int32_t pos) {
  for (auto& slot : slots_) {
    if (slot.pos >= pos) {
      slot.pos = -1;
    }
  }
}

std::optional<std::vector<float>> LlamaPromptHead::Predict(int32_t end_pos) const {
  const int32_t count = std::min(end_pos, static_cast<int32_t>(slots_.size()));
  if (count <= 0) {
    return std::nullopt;
  }
  const size_t n_layers = layer_tensors_.size();
  const uint32_t all = n_layers == 32 ? ~0u : (1u << n_layers) - 1;
  for (int32_t pos = end_pos - count; pos < end_pos; ++pos) {
    const auto& slot = slots_[static_cast<size_t>(pos) % slots_.size()];
    if (slot.pos != pos || slot.filled != all) {
      return std::nullopt;
    }
  }

  const size_t width = static_cast<size_t>(n_embd_);
  std::vector<float> features(n_layers * 2 * width);
  const auto& last = slots_[static_cast<size_t>(end_pos - 1) % slots_.size()].rows;
  for (size_t layer = 0; layer < n_layers; ++layer) {
    std::copy_n(last.begin() + layer * width, width, features.begin() + layer * 2 * width);
  }
  std::vector<double> sums(n_layers * width, 0.0);
  for (int32_t pos = end_pos - count; pos < end_pos; ++pos) {
    const auto& rows = slots_[static_cast<size_t>(pos) % slots_.size()].rows;
    for (size_t i = 0; i < sums.size(); ++i) {
      sums[i] += rows[i];
    }
  }
  for (size_t layer = 0; layer < n_layers; ++layer) {
    for (size_t i = 0; i < width; ++i) {
      features[(layer * 2 + 1) * width + i] = static_cast<float>(sums[layer * width + i] / count);
    }
  }
  return Forward(features);
}

std::vector<float> LlamaPromptHead::Forward(const std::vector<float>& features) const {
  const int64_t hidden_size = w0_->ne[1];
  std::vector<float> row(features.size());
  std::vector<float> hidden(static_cast<size_t>(hidden_size));
  RowToFloat(b0_, 0, hidden.data());
  for (int64_t o = 0; o < hidden_size; ++o) {
    RowToFloat(w0_, o, row.data());
    float sum = hidden[o];
    for (size_t i = 0; i < features.size(); ++i) {
      sum += row[i] * features[i];
    }
    hidden[o] = 0.5f * sum * (1.0f + std::erf(sum / std::sqrt(2.0f)));
  }

  const int64_t output_size = w1_->ne[1];
  std::vector<float> scores(static_cast<size_t>(output_size));
  RowToFloat(b1_, 0, scores.data());
  row.resize(static_cast<size_t>(hidden_size));
  for (int64_t o = 0; o < output_size; ++o) {
    RowToFloat(w1_, o, row.data());
    for (int64_t i = 0; i < hidden_size; ++i) {
      scores[o] += row[i] * hidden[i];
    }
  }

  const float max = *std::max_element(scores.begin(), scores.end());
  float total = 0.0f;
  for (auto& score : scores) {
    score = std::exp(score - max);
    total += score;
  }
  for (auto& score : scores) {
    score /= total;
  }
  return scores;
}

} // namespace giga_edge
