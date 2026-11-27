#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

#include <vector>

namespace giga_edge {

class TransposeNode : public NodeBase {
public:
  TransposeNode(const NodeBaseConfig& base_config, const TransposeNodeParams& params);

  void ProcessOnce() override;

  void Reset() override {};

  REGISTER_INPUT(
    transpose, input, Input, std::vector<float>, "Row-major matrix [rows, columns]; rows = size / columns"
  );
  REGISTER_OUTPUT(transpose, output, Output, std::vector<float>, "Row-major matrix [columns, rows]");

private:
  TransposeNodeParams params_;
};

REGISTER_NODE(
  transpose,
  TransposeNode,
  TransposeNodeParams,
  "Transposes a row-major matrix with `columns` elements per row, e.g. frame-major features [T, mels] "
  "into the mel-major layout [mels, T] an encoder expects. Empty input gives empty output"
);

} // namespace giga_edge
