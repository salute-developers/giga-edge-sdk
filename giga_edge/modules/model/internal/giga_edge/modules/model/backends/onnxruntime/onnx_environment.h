#pragma once

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/environment/environment.h"

#include "onnxruntime/onnxruntime_cxx_api.h"

#include <memory>

namespace giga_edge {

struct OnnxEnvironmentParams {
  int intra_op_num_threads = 1;
  int inter_op_num_threads = 1;
};

template<>
OnnxEnvironmentParams ParseConfig(const YAML::Node& config);

struct OnnxEnvironment : Environment {
  Ort::Env env;

  explicit OnnxEnvironment(const OnnxEnvironmentParams& params);
};

inline const bool kOnnxEnvironmentRegistered = RegisterExternalEnvironment(
  "onnx",
  ExternalEnvironmentHandlers{
    [](const YAML::Node& node) -> std::shared_ptr<void> {
      return std::make_shared<OnnxEnvironmentParams>(ParseConfig<OnnxEnvironmentParams>(node));
    },
    [](const std::shared_ptr<void>& cfg) -> EnvironmentPtr {
      return std::make_shared<OnnxEnvironment>(*std::static_pointer_cast<OnnxEnvironmentParams>(cfg));
    },
  }
);

} // namespace giga_edge
