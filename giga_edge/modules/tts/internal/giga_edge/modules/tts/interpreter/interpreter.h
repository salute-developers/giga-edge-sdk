#pragma once

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/data_reader/data_reader.h"

#include <any>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace giga_edge {

struct Environment;
using EnvironmentPtr = std::shared_ptr<Environment>;

// Backend params: whatever parsing its tts_interpreter_params returned; the module does not need the type.
using TtsInterpreterParams = std::any;

class ITtsInterpreter {
public:
  virtual ~ITtsInterpreter() = default;

  virtual bool Initialize(
    const TtsInterpreterParams& params,
    const IDataReaderPtr& data_reader,
    const std::map<std::string, EnvironmentPtr>* environments = nullptr
  ) = 0;

  virtual bool StartSynthesis(const std::string& text) = 0;

  virtual std::vector<float> GetAudioChunk() = 0;

  virtual bool IsFinished() const = 0;

  virtual void StopSynthesis() = 0;

  virtual bool IsReady() const = 0;

  // Voice of the next phrases; empty means the configured one. false: there is no such voice, synthesis uses the
  // configured one.
  virtual bool SetVoice(const std::string& voice) {
    return voice.empty();
  }
};

struct TtsBackend {
  std::function<TtsInterpreterParams(const YAML::Node& params)> parse_params;
  std::function<std::unique_ptr<ITtsInterpreter>()> create_interpreter;
};

// Synthesis backends by the name from backend_type of the node config.
class TtsBackendRegistry {
public:
  static bool Register(const std::string& name, TtsBackend backend);
  static TtsInterpreterParams ParseParams(const std::string& name, const YAML::Node& params);
  static std::unique_ptr<ITtsInterpreter> CreateInterpreter(const std::string& name);
};

} // namespace giga_edge

// Registers a backend for `backend_type: <name>`: the params are ParseConfig<params_type>, the interpreter is
// interpreter_type.
#define REGISTER_TTS_BACKEND(name, params_type, interpreter_type)                                                      \
  namespace giga_edge {                                                                                                \
  [[maybe_unused]] inline const bool kTtsBackendRegistered_##name = ::giga_edge::TtsBackendRegistry::Register(         \
    #name,                                                                                                             \
    ::giga_edge::TtsBackend{                                                                                           \
      [](const YAML::Node& params) -> ::giga_edge::TtsInterpreterParams {                                              \
        return ::giga_edge::ParseConfig<params_type>(params);                                                          \
      },                                                                                                               \
      []() -> std::unique_ptr<::giga_edge::ITtsInterpreter> {                                                          \
        return std::make_unique<interpreter_type>();                                                                   \
      }                                                                                                                \
    }                                                                                                                  \
  );                                                                                                                   \
  }
