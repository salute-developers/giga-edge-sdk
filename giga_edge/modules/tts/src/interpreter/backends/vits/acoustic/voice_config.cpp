#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/voice_config.h"

#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/utf8.h"

#include <json/reader.h>
#include <json/writer.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace giga_edge::vits {
namespace {

// Bounds that keep the windows, the rate and the labels far inside int and the buffers of a step small.
constexpr int64_t kMaxWindowFrames = 1024;
constexpr int64_t kMinSampleRate = 8000;
constexpr int64_t kMaxSampleRate = 192000;
constexpr int64_t kMaxEmbeddingDim = 256;

class ConfigReader {
public:
  explicit ConfigReader(const std::string& name)
    : name_(name) {}

  [[noreturn]] void Fail(const std::string& message) const {
    throw std::runtime_error("vits voice config '" + name_ + "': " + message);
  }

  void Check(bool condition, const std::string& message) const {
    if (!condition) {
      Fail(message);
    }
  }

  const Json::Value& Field(const Json::Value& object, const char* key) const {
    Check(object.isMember(key), std::string("missing '") + key + "'");
    return object[key];
  }

  int64_t Integer(const Json::Value& object, const char* key, int64_t min, int64_t max) const {
    const auto& value = Field(object, key);
    Check(
      value.isInt64() && value.asInt64() >= min && value.asInt64() <= max,
      std::string("'") + key + "' must be an integer in [" + std::to_string(min) + ", " + std::to_string(max) + "]"
    );
    return value.asInt64();
  }

  // A finite number that stays finite as a float.
  float Number(const Json::Value& object, const char* key) const {
    const auto& value = Field(object, key);
    Check(value.isNumeric() && std::isfinite(value.asFloat()), std::string("'") + key + "' must be a finite number");
    return value.asFloat();
  }

  bool Bool(const Json::Value& object, const char* key) const {
    const auto& value = Field(object, key);
    Check(value.isBool(), std::string("'") + key + "' must be true or false");
    return value.asBool();
  }

  // A key with another value than the one the backend supports; an absent key is the supported one.
  void Supported(const Json::Value& root, const char* key, const Json::Value& value) const {
    if (root.isMember(key) && root[key] != value) {
      Json::StreamWriterBuilder writer;
      writer["indentation"] = "";
      Fail(
        std::string("'") + key + "' = " + Json::writeString(writer, root[key]) + " is not supported, only "
        + Json::writeString(writer, value)
      );
    }
  }

  // An object {feature name: label}; a label is a value of the feature, not "no value".
  std::vector<FeatureLabel> Labels(const Json::Value& root, const char* key, const VoiceConfig& config) const {
    std::vector<FeatureLabel> result;
    if (!root.isMember(key)) {
      return result;
    }
    const auto& labels = root[key];
    Check(labels.isObject(), std::string("'") + key + "' must be an object");
    for (const auto& name : labels.getMemberNames()) {
      const auto found = std::find(config.feature_names.begin(), config.feature_names.end(), name);
      Check(found != config.feature_names.end(), std::string("'") + key + "' names an unknown feature '" + name + "'");
      const auto feature = static_cast<size_t>(found - config.feature_names.begin());
      const auto& label = labels[name];
      Check(
        label.isInt64() && label.asInt64() >= 0 && label.asInt64() < config.embedding_dims[feature],
        std::string("'") + key + "': the label of '" + name + "' must be an integer in [0, "
          + std::to_string(config.embedding_dims[feature]) + ")"
      );
      result.push_back({feature, label.asInt64()});
    }
    return result;
  }

private:
  std::string name_;
};

} // namespace

Json::Value ParseJson(std::string_view text, const std::string& name) {
  Json::CharReaderBuilder builder;
  builder["failIfExtra"] = true;
  builder["rejectDupKeys"] = true;
  const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
  Json::Value root;
  std::string error;
  if (!reader->parse(text.data(), text.data() + text.size(), &root, &error)) {
    throw std::runtime_error("'" + name + "' is not valid JSON: " + error);
  }
  return root;
}

VoiceConfig ParseVoiceConfig(std::string_view json, const std::string& name) {
  const ConfigReader reader(name);
  const auto root = ParseJson(json, name);
  reader.Check(root.isObject(), "must be a JSON object");
  VoiceConfig config;

  const auto& alphabet = reader.Field(root, "supported_characters");
  reader.Check(
    alphabet.isString() && !alphabet.asString().empty(), "'supported_characters' must be a non-empty string"
  );
  const auto symbols = alphabet.asString();
  for (size_t pos = 0; pos < symbols.size();) {
    CodePoint symbol;
    try {
      symbol = DecodeUtf8(symbols, pos);
    } catch (const std::invalid_argument&) {
      reader.Fail("'supported_characters' is not valid UTF-8");
    }
    reader.Check(
      config.alphabet.find(symbol.value) == std::u32string::npos,
      "'supported_characters' repeats '" + symbols.substr(pos, symbol.size) + "'"
    );
    config.alphabet.push_back(symbol.value);
    pos += symbol.size;
  }

  const auto& names = reader.Field(root, "feature_names");
  const auto& dims = reader.Field(root, "embedding_dims");
  reader.Check(
    names.isArray() && !names.empty() && dims.isArray() && dims.size() == names.size(),
    "'feature_names' and 'embedding_dims' must be non-empty arrays of the same size"
  );
  for (Json::ArrayIndex i = 0; i < names.size(); ++i) {
    reader.Check(names[i].isString() && !names[i].asString().empty(), "'feature_names' must hold non-empty strings");
    reader.Check(
      std::count(config.feature_names.begin(), config.feature_names.end(), names[i].asString()) == 0,
      "'feature_names' repeats '" + names[i].asString() + "'"
    );
    reader.Check(
      dims[i].isInt64() && dims[i].asInt64() >= 1 && dims[i].asInt64() <= kMaxEmbeddingDim,
      "'embedding_dims' must hold integers in [1, " + std::to_string(kMaxEmbeddingDim) + "]"
    );
    config.feature_names.push_back(names[i].asString());
    config.embedding_dims.push_back(dims[i].asInt64());
  }
  config.forced_labels = reader.Labels(root, "force_feature_label_dict", config);
  config.question_labels = reader.Labels(root, "question_label_dict", config);
  config.use_question_rules = root.isMember("use_questions_rules") && reader.Bool(root, "use_questions_rules");
  if (root.isMember("pause_threshold")) {
    config.pause_threshold = reader.Number(root, "pause_threshold");
    reader.Check(config.pause_threshold >= 0 && config.pause_threshold <= 1, "'pause_threshold' must be in [0, 1]");
  }
  // Keys that change the text or the model inputs: the backend supports one combination of them.
  Json::Value pause_rules(Json::arrayValue);
  for (const char* rule : {"punkt_to_pause", "no_pause_words", "clean"}) {
    pause_rules.append(rule);
  }
  reader.Supported(root, "use_quantization", true);
  reader.Supported(root, "layer_num", 1);
  reader.Supported(root, "replace_excl_by_period", false);
  reader.Supported(root, "pause_rules", pause_rules);

  config.blank_intersperse = reader.Bool(root, "blank_intersperse");
  config.length_scale = reader.Number(root, "length_scale");
  reader.Check(config.length_scale > 0, "'length_scale' must be > 0");
  config.noise_scale = reader.Number(root, "noise_scale");
  reader.Check(config.noise_scale >= 0, "'noise_scale' must be >= 0");
  config.noise_scale_w = reader.Number(root, "noise_scale_w");
  reader.Check(config.noise_scale_w >= 0, "'noise_scale_w' must be >= 0");
  config.gain_db = reader.Number(root, "gain_db");
  config.sample_rate = reader.Integer(root, "model_sample_rate", kMinSampleRate, kMaxSampleRate);
  if (root.isMember("n_speakers")) {
    config.speakers = reader.Integer(root, "n_speakers", 1, kMaxSpeakers);
  }

  const auto& streaming = reader.Field(root, "cpu_vits_configuration");
  reader.Check(streaming.isObject(), "'cpu_vits_configuration' must be an object");
  config.streaming.num_frames = reader.Integer(streaming, "num_frames", 1, kMaxWindowFrames);
  config.streaming.flow_overlap = reader.Integer(streaming, "flow_overlap", 0, kMaxWindowFrames);
  config.streaming.decoder_overlap = reader.Integer(streaming, "decoder_overlap", 0, kMaxWindowFrames);
  reader.Check(
    config.streaming.decoder_overlap < config.streaming.num_frames, "'decoder_overlap' must be less than 'num_frames'"
  );
  return config;
}

std::string ReadText(const IDataReaderPtr& data_reader, const std::string& path) {
  return ToString(EnsureNotNull(data_reader->ReadTransientData(path), path));
}

VoiceConfig ReadVoiceConfig(const IDataReaderPtr& data_reader, const std::string& path) {
  return ParseVoiceConfig(ReadText(data_reader, path), path);
}

std::vector<int64_t> EncodeText(std::string_view marked, const VoiceConfig& voice, size_t max_symbols) {
  std::vector<int64_t> ids;
  for (size_t pos = 0, symbols = 0; pos < marked.size(); ++symbols) {
    if (symbols == max_symbols) {
      throw std::invalid_argument("vits: a sentence of more than " + std::to_string(max_symbols) + " symbols");
    }
    const auto symbol = DecodeUtf8(marked, pos);
    const auto id = voice.alphabet.find(symbol.value);
    if (id == std::u32string::npos) {
      throw std::invalid_argument(
        "vits: symbol '" + std::string(marked.substr(pos, symbol.size)) + "' at byte " + std::to_string(pos)
        + " is not in the voice alphabet"
      );
    }
    if (voice.blank_intersperse) {
      ids.push_back(0);
    }
    ids.push_back(static_cast<int64_t>(id));
    pos += symbol.size;
  }
  if (voice.blank_intersperse) {
    ids.push_back(0);
  }
  return ids;
}

} // namespace giga_edge::vits
