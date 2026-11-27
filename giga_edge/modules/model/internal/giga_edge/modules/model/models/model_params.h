#pragma once

#include "giga_edge/modules/model/interpreter/params.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace giga_edge {

struct ModelParams {
  using ConstInputData = std::variant<std::vector<float>, std::vector<int64_t>, std::vector<int32_t>>;
  InterpreterParams interpreter;
  int inputs_count = -1;
  std::unordered_map<std::string, ConstInputData> const_inputs;
  std::unordered_map<std::string, int> mutable_dims; // the index of a mutable dimension
  // When a node input is a tensor with more than one mutable dimension, its shape must be
  // supplied as a separate node input. This map binds a tensor input to its shape input
  // in the form {shape_input: tensor_input}.
  std::unordered_map<std::string, std::string> shape_inputs;
  std::unordered_map<std::string, std::vector<int>> initial_state_shape_overrides;
};

} // namespace giga_edge
