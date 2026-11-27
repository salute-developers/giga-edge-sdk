#pragma once

namespace giga_edge {

struct TfLiteInterpreterParams {
  // Run the operations XNNPACK supports through the XNNPACK delegate. Requires tflite built with XNNPACK
  // (build_giga_edge.sh --tflite-xnnpack). Float outputs differ from the default tflite kernels in the last bits.
  bool use_xnnpack = false;
};

} // namespace giga_edge
