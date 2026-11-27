#include "giga_edge/modules/model/backends/onnxruntime/onnx_environment.h"

namespace giga_edge {

namespace {

Ort::Env CreateEnv(const OnnxEnvironmentParams& params) {
  Ort::ThreadingOptions options;

  options.SetGlobalIntraOpNumThreads(params.intra_op_num_threads);
  options.SetGlobalInterOpNumThreads(params.inter_op_num_threads);

  return Ort::Env(options, ORT_LOGGING_LEVEL_WARNING, "giga-edge-onnx-global");
}

} // namespace

template<>
OnnxEnvironmentParams ParseConfig(const YAML::Node& config) {
  OnnxEnvironmentParams result;
  if (config) {
    result.intra_op_num_threads = ParseThreadCount(config["num_intra_threads"], 1);
    result.inter_op_num_threads = config["num_inter_threads"].as<int>(1);
  }
  return result;
}

OnnxEnvironment::OnnxEnvironment(const OnnxEnvironmentParams& params)
  : env(CreateEnv(params)) {}

} // namespace giga_edge
