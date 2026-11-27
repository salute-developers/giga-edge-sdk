#include "giga_edge/modules/model/config_parser/common/model.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/modules/model/models/crnn/params.h"
#include "giga_edge/modules/model/models/model_params.h"

#include <functional>
#include <map>
#include <stdexcept>
#include <vector>

namespace giga_edge {

namespace {

template<typename T>
std::vector<T> ParseVectorFromNode(const YAML::Node& values_node) {
  std::vector<T> values;
  values.reserve(values_node.size());
  for (const auto& item : values_node) {
    values.push_back(item.as<T>());
  }
  return values;
}

using ConstInputParser = std::function<ModelParams::ConstInputData(const YAML::Node&)>;

const static std::map<std::string, ConstInputParser> g_type_parsers = {
  {"float",
   [](const YAML::Node& n) {
     return ParseVectorFromNode<float>(n);
   }},
  {"int64",
   [](const YAML::Node& n) {
     return ParseVectorFromNode<int64_t>(n);
   }},
  {"int32", [](const YAML::Node& n) {
     return ParseVectorFromNode<int32_t>(n);
   }}
};

} // namespace

template<>
ModelParams ParseConfig(const YAML::Node& config) {
  ModelParams result;
  result.interpreter = ParseConfig<InterpreterParams>(config["interpreter"]);
  result.inputs_count = config["inputs_count"].as<int>(-1);

  if (config["const_inputs"]) {
    for (const auto& input : config["const_inputs"]) {
      const auto name = input.first.as<std::string>();
      const auto& value_node = input.second;

      ModelParams::ConstInputData data;

      if (value_node.IsSequence()) { // a plain list of floats
        data = ParseVectorFromNode<float>(value_node);
      } else if (value_node.IsMap()) { // {type, value}
        const auto type = value_node["type"].as<std::string>("float");
        if (!value_node["value"] || !value_node["value"].IsSequence()) {
          throw std::runtime_error("Const input '" + name + "' must have a 'value' sequence.");
        }

        const auto& values_node = value_node["value"];

        auto it = g_type_parsers.find(type);
        if (it == g_type_parsers.end()) {
          throw std::runtime_error("Unknown type '" + type + "' for const_input '" + name + "'");
        }

        data = it->second(values_node);

      } else {
        throw std::runtime_error("Invalid format for const input '" + name + "'. Must be a sequence or a map.");
      }

      auto [it, success] = result.const_inputs.try_emplace(name, std::move(data));
      if (!success) {
        throw std::runtime_error("Multiple const inputs for model with the same name: " + name);
      }
    }
  }

  for (const auto& input : config["mutable_dims"]) {
    auto [it, success] = result.mutable_dims.try_emplace(input.first.as<std::string>(), input.second.as<int>());
    if (!success) {
      throw std::runtime_error("Multiple mutable dims for model with the same name: " + input.first.as<std::string>());
    }
  }

  for (const auto& input : config["shape_inputs"]) {
    auto [it, success] = result.shape_inputs.try_emplace(input.first.as<std::string>(), input.second.as<std::string>());
    if (!success) {
      throw std::runtime_error("Multiple shape inputs for model with the same name: " + input.first.as<std::string>());
    }
  }

  if (config["initial_state_shape_overrides"]) {
    for (const auto& override_node : config["initial_state_shape_overrides"]) {
      const auto prefix = override_node.first.as<std::string>();
      std::vector<int> shape;
      if (override_node.second.IsSequence()) {
        for (const auto& dim : override_node.second) {
          shape.push_back(dim.as<int>());
        }
      }
      result.initial_state_shape_overrides[prefix] = shape;
    }
  }

  return result;
}

template<>
CRNNModelParams ParseConfig(const YAML::Node& config) {
  CRNNModelParams result;

  result.interpreter_params = ParseConfig<InterpreterParams>(config["interpreter"]);
  result.num_mels = config["num_mels"].as<int>();
  return result;
}

template<>
CRNNSlidingWindowParams ParseConfig(const YAML::Node& config) {
  CRNNSlidingWindowParams result;

  result.num_classes = config["num_classes"].as<int>();
  result.window_size = config["size"].as<int>();
  return result;
}

template<>
WindowCRNNModelParams ParseConfig(const YAML::Node& config) {
  WindowCRNNModelParams result;

  result.step_params = ParseConfig<CRNNModelParams>(config["step_model"]);
  result.window_params = ParseConfig<CRNNSlidingWindowParams>(config["window"]);
  return result;
}

} // namespace giga_edge
