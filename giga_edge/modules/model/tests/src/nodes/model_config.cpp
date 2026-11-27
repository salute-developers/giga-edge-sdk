#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/model/nodes/model/model.h"

#include <catch.hpp>

#include <vector>

using namespace giga_edge;

namespace giga_edge {

inline bool operator==(const InferenceStrategyParams& lhs, const InferenceStrategyParams& rhs) {
  return lhs.type == rhs.type;
}

inline bool operator==(const ModelNodeParams& lhs, const ModelNodeParams& rhs) {
  return lhs.path == rhs.path && lhs.interpreter.type == rhs.interpreter.type && lhs.inputs_count == rhs.inputs_count
    && lhs.const_inputs == rhs.const_inputs && lhs.mutable_dims == rhs.mutable_dims
    && lhs.shape_inputs == rhs.shape_inputs;
}

} // namespace giga_edge

TEST_CASE("Model node, test parse config", "[runtime][nodes][tflite]") {
  REQUIRE(ParseConfig<ModelNodeParams>(YAML::Load(
    "path: path\n"
    "interpreter:\n"
    "  type: tflite\n"
    "inputs_count: 10\n"
  )) == ModelNodeParams{
      {
        .interpreter = {
          .type = InterpreterParams::InterpreterType::TFLITE,
        },
        .inputs_count = 10,
      },
      "path",
      {
        .type = InferenceStrategyType::SYNC_INFERENCE,
      },
  });

  REQUIRE(ParseConfig<ModelNodeParams>(YAML::Load(
    "path: path\n"
    "interpreter:\n"
    "  type: tflite\n"
    "inputs_count: 10\n"
    "const_inputs:\n"
    "  input_1: [1, 2, 3]\n"
    "  input_2: [1.5]"

  )) == ModelNodeParams{
      {
        .interpreter = {
          .type = InterpreterParams::InterpreterType::TFLITE,
        },
        .inputs_count = 10,
        .const_inputs = {
          {"input_1", std::vector<float>{1.0, 2.0, 3.0}},
          {"input_2", std::vector<float>{1.5}},
        },
      },
      "path",
      {
        .type = InferenceStrategyType::SYNC_INFERENCE,
      },
  });

  REQUIRE(ParseConfig<ModelNodeParams>(YAML::Load(
    "path: path\n"
    "interpreter:\n"
    "  type: tflite\n"
    "inputs_count: 10\n"
    "const_inputs:\n"
    "  input_1: [1, 2, 3]\n"
    "  input_2: [1.5]\n"
    "mutable_dims:\n"
    "  input_1: 0\n"
    "  input_2: 2\n"
    "allow_resizable_recursive_state: True"
  )) == ModelNodeParams{
      {
        .interpreter = {
          .type = InterpreterParams::InterpreterType::TFLITE,
        },
        .inputs_count = 10,
        .const_inputs = {
          {"input_1", std::vector<float>{1.0, 2.0, 3.0}},
          {"input_2", std::vector<float>{1.5}},
        },
        .mutable_dims = {
          {"input_1", 0},
          {"input_2", 2},
        },
      },
      "path",
      {
        .type = InferenceStrategyType::SYNC_INFERENCE,
      },
  });

  REQUIRE(ParseConfig<ModelNodeParams>(YAML::Load(
    "path: path\n"
    "interpreter:\n"
    "  type: tflite\n"
    "inputs_count: 10\n"
    "shape_inputs:\n"
    "  x_shape: x\n"
  )) == ModelNodeParams{
      {
        .interpreter = {
          .type = InterpreterParams::InterpreterType::TFLITE,
        },
        .inputs_count = 10,
        .shape_inputs = {
          {"x_shape", "x"},
        },
      },
      "path",
      {
        .type = InferenceStrategyType::SYNC_INFERENCE,
      },
  });
}
