#include "giga_edge/modules/model/interpreter/interpreter.h"

#include "giga_edge/lib/common/os.h"

#include <stdexcept>
#include <string>
#include <utility>

#if defined(OS_APPLE) && defined(COREML_ENABLED)
#include "giga_edge/modules/model/backends/coreml/interpreter.h"
#endif

#ifdef TFLITE_ENABLED
#include "giga_edge/modules/model/backends/tflite/interpreter.h"
#endif

#ifdef ONNXRUNTIME_ENABLED
#include "giga_edge/modules/model/backends/onnxruntime/interpreter.h"
#endif

#ifdef NCNN_ENABLED
#include "giga_edge/modules/model/backends/ncnn/interpreter.h"
#endif

namespace giga_edge {

float* Interpreter::InputFloatData(const std::string& name) const {
  return Input(name)->FloatData();
}

float* Interpreter::OutputFloatData(const std::string& name) const {
  return Output(name)->FloatData();
}

int* Interpreter::InputIntData(const std::string& name) const {
  return Input(name)->IntData();
}

int* Interpreter::OutputIntData(const std::string& name) const {
  return Output(name)->IntData();
}

void Interpreter::ResizeInput(
  [[maybe_unused]] int id, [[maybe_unused]] const int* dims, [[maybe_unused]] int dims_count
) {
  throw std::runtime_error("Interpreter::ResizeInput is not implemented");
}

void Interpreter::ResizeInput(
  [[maybe_unused]] const std::string& name, [[maybe_unused]] const int* dims, [[maybe_unused]] int dims_count
) {
  throw std::runtime_error("Interpreter::ResizeInput is not implemented");
}

void Interpreter::ReallocateTensors() {
  throw std::runtime_error("Interpreter::ReallocateTensors is not implemented");
}

InterpreterPtr MakeInterpreter(
  const InterpreterParams& config,
  const std::string& path,
  const IDataReaderPtr& data_reader,
  [[maybe_unused]] const std::map<std::string, EnvironmentPtr>* environments
) {
  switch (config.type) {
    case InterpreterParams::InterpreterType::COREML:
#if defined(OS_APPLE) && defined(COREML_ENABLED)
      return std::make_shared<giga_edge::CoreMLInterpreter>(config.coreml_params, path, data_reader);
#else
      throw std::runtime_error("CoreML interpreter is not supported");
#endif

    case InterpreterParams::InterpreterType::TFLITE:
#ifdef TFLITE_ENABLED
      return std::make_shared<giga_edge::TfLiteInterpreter>(config.tflite_params, path, data_reader);
#else
      throw std::runtime_error("TfLite interpreter is not supported");
#endif

    case InterpreterParams::InterpreterType::ONNX:
#ifdef ONNXRUNTIME_ENABLED
      return std::make_shared<giga_edge::OnnxruntimeInterpreter>(config.onnx_params, path, data_reader, environments);
#else
      throw std::runtime_error("ONNX Runtime interpreter is not supported");
#endif

    case InterpreterParams::InterpreterType::NCNN:
#ifdef NCNN_ENABLED
      return std::make_shared<giga_edge::NcnnInterpreter>(path, data_reader);
#else
      throw std::runtime_error("ncnn Runtime interpreter is not supported");
#endif
  }

  throw std::invalid_argument("Invalid model interpreter type");
}

} // namespace giga_edge
