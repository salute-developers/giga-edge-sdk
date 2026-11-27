#pragma once

namespace giga_edge {

enum class CoreMLComputeUnits { CPU_ONLY, CPU_AND_GPU, ALL, CPU_AND_NEURAL_ENGINE };

struct CoreMLInterpreterParams {
  CoreMLComputeUnits compute_units = CoreMLComputeUnits::ALL;
  // The first Neural Engine prediction specializes the model (seconds): pay it at load.
  bool warmup = false;
};

} // namespace giga_edge
