#include "giga_edge/modules/model/config_parser/common/interpreter.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/enum/enum.h"
#include "giga_edge/modules/model/interpreter/params.h"

#include <stdexcept>

#ifdef ONNXRUNTIME_ENABLED

GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(giga_edge::OnnxExecutionMode, "sequential", "parallel");
GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(giga_edge::OnnxOptimizationLevel, "disable", "basic", "extended", "all");

#endif

#ifdef COREML_ENABLED

GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(
  giga_edge::CoreMLComputeUnits, "cpu_only", "cpu_and_gpu", "all", "cpu_and_neural_engine"
);

#endif

namespace giga_edge {

#ifdef COREML_ENABLED
template<>
CoreMLInterpreterParams ParseConfig(const YAML::Node& config) {
  CoreMLInterpreterParams result;
  if (config) {
    result.compute_units = EnumFromString<CoreMLComputeUnits>(config["compute_units"].as<std::string>("all"));
    result.warmup = config["warmup"].as<bool>(false);
  }
  return result;
}
#endif

#ifdef TFLITE_ENABLED
template<>
TfLiteInterpreterParams ParseConfig(const YAML::Node& config) {
  TfLiteInterpreterParams result;
  if (config) {
    result.use_xnnpack = config["use_xnnpack"].as<bool>(false);
  }
  return result;
}
#endif

#ifdef ONNXRUNTIME_ENABLED
template<>
OnnxInterpreterParams ParseConfig(const YAML::Node& config) {
  OnnxInterpreterParams result;
  if (config) {
    result.use_ort_format = config["use_ort_format"].as<bool>(false);
    result.disable_copy_to_ram = config["disable_copy_to_ram"].as<bool>(false);
    result.shrink_arena = config["shrink_arena"].as<bool>(false);

    result.inter_op_num_threads = config["inter_op_num_threads"].as<int>(1);
    result.intra_op_num_threads = ParseThreadCount(config["intra_op_num_threads"], 1);

    result.use_environment = config["use_environment"].as<std::string>("");

    result.execution_mode = EnumFromString<OnnxExecutionMode>(config["execution_mode"].as<std::string>("sequential"));
    result.optimization_level = EnumFromString<OnnxOptimizationLevel>(
      config["optimization_level"].as<std::string>("all")
    );
  }
  return result;
}
#endif

template<>
InterpreterParams ParseConfig(const YAML::Node& config) {
  InterpreterParams result;
  result.type = ParseIntepreterType(config["type"].as<std::string>(DEFAULT_INTERPPRETER_TYPE));

#ifdef COREML_ENABLED
  if (result.type == InterpreterParams::InterpreterType::COREML) {
    result.coreml_params = ParseConfig<CoreMLInterpreterParams>(config["coreml"]);
  }
#endif

#ifdef TFLITE_ENABLED
  if (result.type == InterpreterParams::InterpreterType::TFLITE) {
    result.tflite_params = ParseConfig<TfLiteInterpreterParams>(config["tflite"]);
  }
#endif

#ifdef ONNXRUNTIME_ENABLED
  if (result.type == InterpreterParams::InterpreterType::ONNX) {
    result.onnx_params = ParseConfig<OnnxInterpreterParams>(config["onnx"]);
  }
#endif

  return result;
}

} // namespace giga_edge
