#pragma once

#include "giga_edge/modules/tts/interpreter/interpreter.h"

#include <string>

namespace giga_edge {

struct TtsNodeParams {
  std::string backend_type;
  TtsInterpreterParams tts_interpreter_params;
  // Empty: synthesis runs inside the tick; otherwise in the thread pool of the environment, and the tick outputs the
  // ready chunks.
  std::string async_thread_pool;
  int async_thread_priority = 0;
};

} // namespace giga_edge
