#include "giga_edge/modules/tts/nodes/tts_streaming_audio_output/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

#include <stdexcept>

namespace giga_edge {

template<>
TtsNodeParams ParseConfig(const YAML::Node& config) {
  TtsNodeParams result;

  if (!config["backend_type"]) {
    throw std::runtime_error("TtsNode requires 'backend_type'.");
  }
  result.backend_type = config["backend_type"].as<std::string>();

  if (config["tts_interpreter_params"]) {
    result.tts_interpreter_params = TtsBackendRegistry::ParseParams(
      result.backend_type, config["tts_interpreter_params"]
    );
  } else {
    throw std::runtime_error("TtsNode requires 'tts_interpreter_params' section.");
  }

  if (const auto strategy = config["inference_strategy"]; strategy) {
    const auto type = strategy["type"].as<std::string>("sync");
    if (type == "wait_till_inferring") {
      result.async_thread_pool = strategy["thread_pool_name"].as<std::string>("");
      if (result.async_thread_pool.empty()) {
        throw std::runtime_error("TtsNode: 'thread_pool_name' for async strategy is not specified");
      }
      result.async_thread_priority = strategy["thread_priority"].as<int>(0);
    } else if (type != "sync") {
      throw std::runtime_error("TtsNode: unsupported inference_strategy: " + type);
    }
  }

  return result;
}

} // namespace giga_edge
