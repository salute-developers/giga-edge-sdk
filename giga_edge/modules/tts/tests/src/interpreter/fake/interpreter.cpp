#include "giga_edge/modules/tts/interpreter/interpreter.h"

#include "giga_edge/lib/config_parser/config_parser.h"

#include <any>
#include <cstddef>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// A test-only backend: it is registered from the test binary the way a backend from its own directory would be.
// One chunk per character: samples_per_char samples with the value (character code / 256), negated for the voice
// inverted.
namespace giga_edge::test {

struct FakeTtsParams {
  std::size_t samples_per_char = 2;
  bool fail_initialize = false;
  // StartSynthesis throws on this text.
  std::string fail_text;
  // GetAudioChunk throws on this character.
  std::string fail_char;
};

class FakeTtsInterpreter : public ITtsInterpreter {
public:
  bool Initialize(
    const TtsInterpreterParams& params,
    const IDataReaderPtr& /*data_reader*/,
    const std::map<std::string, EnvironmentPtr>* /*environments*/
  ) override {
    params_ = std::any_cast<const FakeTtsParams&>(params);
    ready_ = !params_.fail_initialize;
    return ready_;
  }

  bool StartSynthesis(const std::string& text) override {
    // After a failure the node has to call StopSynthesis.
    if (failed_) {
      throw std::logic_error("fake backend was not stopped after a failure");
    }
    if (!params_.fail_text.empty() && text == params_.fail_text) {
      failed_ = true;
      throw std::runtime_error("fake synthesis failed");
    }
    text_ = text;
    next_ = 0;
    return true;
  }

  std::vector<float> GetAudioChunk() override {
    if (!params_.fail_char.empty() && text_.at(next_) == params_.fail_char.front()) {
      failed_ = true;
      throw std::runtime_error("fake chunk failed");
    }
    const float sample = static_cast<float>(static_cast<unsigned char>(text_.at(next_++))) / 256.0F;
    return std::vector<float>(params_.samples_per_char, voice_ == "inverted" ? -sample : sample);
  }

  bool IsFinished() const override {
    return next_ >= text_.size();
  }

  void StopSynthesis() override {
    failed_ = false;
    text_.clear();
    next_ = 0;
  }

  bool IsReady() const override {
    return ready_;
  }

  bool SetVoice(const std::string& voice) override {
    const bool known = voice.empty() || voice == "inverted";
    voice_ = known ? voice : std::string();
    return known;
  }

private:
  FakeTtsParams params_;
  bool ready_ = false;
  bool failed_ = false;
  std::string text_;
  std::size_t next_ = 0;
  std::string voice_;
};

} // namespace giga_edge::test

namespace giga_edge {

template<>
test::FakeTtsParams ParseConfig(const YAML::Node& config) {
  test::FakeTtsParams result;
  PARSE_FIELD(samples_per_char);
  PARSE_FIELD(fail_initialize);
  PARSE_FIELD(fail_text);
  PARSE_FIELD(fail_char);
  return result;
}

} // namespace giga_edge

REGISTER_TTS_BACKEND(fake, test::FakeTtsParams, test::FakeTtsInterpreter)
