#pragma once

#include <string>

namespace giga_edge {

enum class OnnxExecutionMode { SEQUENTIAL, PARALLEL };

// Graph optimizations of ONNX Runtime at session creation.
enum class OnnxOptimizationLevel { DISABLE, BASIC, EXTENDED, ALL };

struct OnnxInterpreterParams {
  bool use_ort_format = false;
  bool disable_copy_to_ram = false;
  // The arena releases memory after every run: without this it keeps the peak of a past long recording.
  bool shrink_arena = false;

  int inter_op_num_threads = 1;
  int intra_op_num_threads = 1;

  std::string use_environment = "";

  OnnxExecutionMode execution_mode = OnnxExecutionMode::SEQUENTIAL;

  OnnxOptimizationLevel optimization_level = OnnxOptimizationLevel::ALL;
};

} // namespace giga_edge
