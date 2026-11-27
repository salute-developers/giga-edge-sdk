#include "giga_edge/modules/common/nodes/transpose/node.h"

#include <stdexcept>
#include <string>

namespace giga_edge {

TransposeNode::TransposeNode(const NodeBaseConfig& base_config, const TransposeNodeParams& params)
  : NodeBase(base_config)
  , params_(params) {
  if (params_.columns <= 0) {
    throw std::runtime_error("TransposeNode: columns must be positive");
  }
}

void TransposeNode::ProcessOnce() {
  const auto& input = GetInput();
  if (input.empty()) {
    PushOutput({});
    return;
  }
  const size_t columns = params_.columns;
  if (input.size() % columns != 0) {
    throw std::runtime_error(
      "TransposeNode: input size " + std::to_string(input.size()) + " is not a multiple of columns "
      + std::to_string(columns)
    );
  }
  const size_t rows = input.size() / columns;
  std::vector<float> output(input.size());
  for (size_t row = 0; row < rows; ++row) {
    for (size_t column = 0; column < columns; ++column) {
      output[column * rows + row] = input[row * columns + column];
    }
  }
  PushOutput(std::move(output));
}

} // namespace giga_edge
