#pragma once

#include "giga_edge/modules/model/models/crnn/params.h"

#include <string>

namespace giga_edge {

struct CRNNModelNodeParams {
  std::string path;

  ModelType type;

  WindowCRNNModelParams window_crnn_model_params;
  CRNNModelParams stateful_crnn_model_params;
};

} // namespace giga_edge