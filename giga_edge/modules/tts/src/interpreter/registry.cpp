#include "giga_edge/modules/tts/interpreter/interpreter.h"

// Backends of the build (the backend.cmake of each): the include registers them here, next to the registry, so they
// are not dropped from the static library.
#include "giga_edge/modules/tts/interpreter/backends.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace giga_edge {
namespace {

std::map<std::string, TtsBackend>& Backends() {
  static std::map<std::string, TtsBackend> backends;
  return backends;
}

const TtsBackend& FindBackend(const std::string& name) {
  const auto it = Backends().find(name);
  if (it == Backends().end()) {
    std::string built_in;
    for (const auto& [backend_name, _] : Backends()) {
      built_in += (built_in.empty() ? "" : ", ") + backend_name;
    }
    throw std::runtime_error("Unknown TTS backend '" + name + "'; built in: " + built_in);
  }
  return it->second;
}

} // namespace

bool TtsBackendRegistry::Register(const std::string& name, TtsBackend backend) {
  if (!Backends().emplace(name, std::move(backend)).second) {
    throw std::runtime_error("TTS backend is already registered: " + name);
  }
  return true;
}

TtsInterpreterParams TtsBackendRegistry::ParseParams(const std::string& name, const YAML::Node& params) {
  return FindBackend(name).parse_params(params);
}

std::unique_ptr<ITtsInterpreter> TtsBackendRegistry::CreateInterpreter(const std::string& name) {
  return FindBackend(name).create_interpreter();
}

} // namespace giga_edge
