#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/bert.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace giga_edge::vits {

Bert::Bert(InterpreterPtr model, WordPiece wordpiece)
  : model_(std::move(model))
  , wordpiece_(std::move(wordpiece)) {
  // The element types the code writes and reads, before any data is touched.
  const std::pair<TensorPtr, Tensor::DataType> tensors[] = {
    {model_->Input("token_ids"), Tensor::DataType::INT64},
    {model_->Input("attention_mask"), Tensor::DataType::INT64},
    {model_->Output("encodings"), Tensor::DataType::FLOAT32},
  };
  for (const auto& [tensor, type] : tensors) {
    if (tensor->GetDataType() != type) {
      throw std::runtime_error("vits: '" + std::string(tensor->GetName()) + "' of bert has another element type");
    }
  }
  // An input more would stay as the model module made it: refused.
  for (const auto& input : model_->InputNames()) {
    if (input != "token_ids" && input != "attention_mask") {
      throw std::runtime_error("vits: bert has an unknown input '" + input + "'");
    }
  }
}

BertEmbeddings Bert::Embed(std::string_view plain) {
  BertEmbeddings result;
  result.tokens = wordpiece_.Tokenize(plain);
  const auto& tokens = result.tokens;
  for (size_t begin = 0; begin < tokens.size(); begin += kWindow) {
    const auto count = std::min(kWindow, tokens.size() - begin);
    const int shape[] = {1, static_cast<int>(count)};
    model_->ResizeInput("token_ids", shape, 2);
    model_->ResizeInput("attention_mask", shape, 2);
    model_->ReallocateTensors();
    auto* ids = model_->Input("token_ids")->Int64Data();
    auto* mask = model_->Input("attention_mask")->Int64Data();
    for (size_t i = 0; i < count; ++i) {
      ids[i] = tokens[begin + i].id;
      mask[i] = 1;
    }
    model_->Invoke();
    const auto output = model_->Output("encodings");
    const auto out_shape = output->GetShape();
    if (
      out_shape.size() != 3 || out_shape[0] != 1 || out_shape[1] != shape[1] || out_shape[2] < 1
      || (result.size != 0 && out_shape[2] != result.size)
    ) {
      throw std::runtime_error("vits: BERT encodings have an unexpected shape");
    }
    result.size = out_shape[2];
    const auto* data = output->FloatData();
    result.values.insert(result.values.end(), data, data + count * static_cast<size_t>(result.size));
  }
  return result;
}

} // namespace giga_edge::vits
