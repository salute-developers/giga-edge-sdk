#pragma once

#include "giga_edge/modules/model/interpreter/params.h"

#include <cstdint>
#include <string>

namespace giga_edge {

enum class ModelType { STATEFUL_CRNN, WINDOW_CRNN };

ModelType ParseModelType(const std::string& model_type);

struct CRNNModelParams {
  InterpreterParams interpreter_params;
  int32_t num_mels;
};

struct CRNNSlidingWindowParams {
  int32_t num_classes;
  int32_t window_size;
};

struct WindowCRNNModelParams {
  CRNNModelParams step_params;
  CRNNSlidingWindowParams window_params;
};

} // namespace giga_edge
