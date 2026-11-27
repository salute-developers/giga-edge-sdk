#include "giga_edge/lib/logical/logical_op.h"

#include "giga_edge/lib/enum/enum.h"
#include "giga_edge/lib/enum/enum_fwd.h"

#include <string>

GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(LogicalOpType, "not", "or", "and");

namespace giga_edge {

bool LogicalOpBinary(bool left, bool right, LogicalOpType logical_type) {
  switch (logical_type) {
    case LogicalOpType::OR:
      return left || right;
    case LogicalOpType::AND:
      return left && right;
    default:
      throw std::invalid_argument(
        std::string{"Invalid logical type for binary function"} + std::string(EnumToString(logical_type))
      );
  }
}

bool LogicalOpUnary(bool input, LogicalOpType logical_type) {
  switch (logical_type) {
    case LogicalOpType::NOT:
      return !input;
    default:
      throw std::invalid_argument(
        std::string("Invalid logical type for unary function") + std::string(EnumToString(logical_type))
      );
  }
}

} // namespace giga_edge
