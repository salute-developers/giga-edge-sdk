#pragma once

#ifdef COREML_ENABLED
#include "giga_edge/modules/model/backends/coreml/params.h"
#endif

#ifdef TFLITE_ENABLED
#include "giga_edge/modules/model/backends/tflite/params.h"
#endif

#ifdef ONNXRUNTIME_ENABLED
#include "giga_edge/modules/model/backends/onnxruntime/params.h"
#endif

namespace giga_edge {

struct InterpreterParams {
  // Explicit values: the types left out of some builds do not renumber the others.
  enum class InterpreterType {
    COREML = 0,
    TFLITE = 2,
    ONNX = 4,
    NCNN = 5,
  } type;

#ifdef COREML_ENABLED
  CoreMLInterpreterParams coreml_params;
#endif

#ifdef TFLITE_ENABLED
  TfLiteInterpreterParams tflite_params;
#endif

#ifdef ONNXRUNTIME_ENABLED
  OnnxInterpreterParams onnx_params;
#endif
};

} // namespace giga_edge
