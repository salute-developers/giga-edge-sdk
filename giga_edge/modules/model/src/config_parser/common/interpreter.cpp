#include "giga_edge/modules/model/config_parser/common/interpreter.h"

#include "giga_edge/lib/common/os.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>

namespace giga_edge {

// CoreML takes only .mlmodelc, so it must be requested explicitly.
const std::string DEFAULT_INTERPPRETER_TYPE = "tflite";

namespace {

#if defined(OS_APPLE) && defined(COREML_ENABLED)
constexpr bool kCoreMlEnabled = true;
#else
constexpr bool kCoreMlEnabled = false;
#endif

#ifdef TFLITE_ENABLED
constexpr bool kTfLiteEnabled = true;
#else
constexpr bool kTfLiteEnabled = false;
#endif

#ifdef ONNXRUNTIME_ENABLED
constexpr bool kOnnxEnabled = true;
#else
constexpr bool kOnnxEnabled = false;
#endif

#ifdef NCNN_ENABLED
constexpr bool kNcnnEnabled = true;
#else
constexpr bool kNcnnEnabled = false;
#endif

struct InterpreterTypeInfo {
  std::string_view name;
  std::string_view title;
  InterpreterParams::InterpreterType type;
  bool enabled;
};

constexpr std::array kInterpreterTypes{
  InterpreterTypeInfo{"coreml", "CoreML", InterpreterParams::InterpreterType::COREML, kCoreMlEnabled},
  InterpreterTypeInfo{"tflite", "TfLite", InterpreterParams::InterpreterType::TFLITE, kTfLiteEnabled},
  InterpreterTypeInfo{"onnx", "ONNX", InterpreterParams::InterpreterType::ONNX, kOnnxEnabled},
  InterpreterTypeInfo{"ncnn", "ncnn", InterpreterParams::InterpreterType::NCNN, kNcnnEnabled},
};

const InterpreterTypeInfo* FindInterpreterType(std::string_view name) {
  const auto it = std::find_if(
    kInterpreterTypes.begin(), kInterpreterTypes.end(), [name](const InterpreterTypeInfo& info) {
      return info.name == name;
    }
  );
  return it == kInterpreterTypes.end() ? nullptr : &*it;
}

} // namespace

bool IsInterpreterTypeSupported(const std::string& type) {
  const auto* info = FindInterpreterType(type);
  return info != nullptr && info->enabled;
}

InterpreterParams::InterpreterType ParseIntepreterType(const std::string& type) {
  const auto* info = FindInterpreterType(type);
  if (info == nullptr) {
    throw std::runtime_error("Unknown interpreter " + type);
  }
  if (!info->enabled) {
    throw std::runtime_error(std::string(info->title) + " interpreter is not supported");
  }
  return info->type;
}

std::string_view InterpreterTypeName(InterpreterParams::InterpreterType type) {
  const auto it = std::find_if(
    kInterpreterTypes.begin(), kInterpreterTypes.end(), [type](const InterpreterTypeInfo& info) {
      return info.type == type;
    }
  );
  if (it == kInterpreterTypes.end()) {
    throw std::invalid_argument("Unknown interpreter type " + std::to_string(static_cast<int>(type)));
  }
  return it->name;
}

} // namespace giga_edge
