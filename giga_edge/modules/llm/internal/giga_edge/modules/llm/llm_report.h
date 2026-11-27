#pragma once

#include "giga_edge/modules/assistant_core/contracts/types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace giga_edge {

struct LlmReport {
  std::string request_id;
  RunnerPhase phase = RunnerPhase::IDLE;
  // Timings are present only on the final of a request.
  GenerationUsage usage;
  bool is_final = false;
  StreamFinishReason finish_reason = StreamFinishReason::NONE;
  AssistantStatus status = AssistantStatus::OK;
  std::string error;
  std::vector<float> prompt_probabilities;
};

} // namespace giga_edge
