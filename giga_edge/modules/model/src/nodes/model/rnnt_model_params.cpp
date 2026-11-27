#include "giga_edge/modules/model/nodes/model/rnnt_model_params.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/enum/enum.h"

#include <limits>
#include <stdexcept>

GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(RNNTModelNodeParams::Joint::Format, "tokens", "logits");

namespace giga_edge {

template<>
RNNTModelNodeParams::Decoder ParseConfig(const YAML::Node& config) {
  RNNTModelNodeParams::Decoder result = {ParseConfig<ModelParams>(config)};

  result.path = config["path"].as<std::string>();
  result.start_input = config["start_input"].as<std::vector<int>>();

  if (const auto& call_limiter = config["call_limiter"]) {
    result.call_limiter.n_frames = call_limiter["n_frames"].as<int>();
    result.call_limiter.limit = call_limiter["limit"].as<int>();
  } else {
    result.call_limiter.n_frames = 1;
    result.call_limiter.limit = std::numeric_limits<int>::max();
  }

  if (result.call_limiter.n_frames <= 0 || result.call_limiter.limit <= 0) {
    throw std::invalid_argument("rnnt_model: call limiter values must be positive");
  }
  return result;
}

template<>
RNNTModelNodeParams::Joint ParseConfig(const YAML::Node& config) {
  RNNTModelNodeParams::Joint result = {ParseConfig<ModelParams>(config)};

  result.path = config["path"].as<std::string>();
  result.format = EnumFromString<RNNTModelNodeParams::Joint::Format>(config["format"].as<std::string>("tokens"));
  result.blank_id = config["blank_id"].as<int>(-1);
  result.blank_penalty = config["blank_penalty"].as<float>(0);
  result.max_symbols_per_step = config["max_symbols_per_step"].as<int>(0);
  if (result.max_symbols_per_step < 0) {
    throw std::runtime_error("rnnt_model: max_symbols_per_step must be non-negative");
  }

  result.consequent_blanks_as_eos = config["consequent_blanks_as_eos"].as<int>(0);
  result.tokens_output_name = config["tokens_output_name"].as<std::string>("");
  result.eos_token_id = config["eos_token_id"].as<int>(-1);

  result.is_interleaved_encoder_feature_copy_strategy = config["is_interleaved_encoder_feature_copy_strategy"].as<bool>(
    true
  );

  return result;
}

template<>
RNNTModelNodeParams ParseConfig(const YAML::Node& config) {
  RNNTModelNodeParams result;
  result.decoder = ParseConfig<RNNTModelNodeParams::Decoder>(config["decoder"]);
  result.joint = ParseConfig<RNNTModelNodeParams::Joint>(config["joint"]);
  if (!config["decoder"]["call_limiter"] && result.joint.max_symbols_per_step == 0) {
    throw std::invalid_argument("rnnt_model: set joint.max_symbols_per_step or decoder.call_limiter");
  }
  return result;
}

} // namespace giga_edge