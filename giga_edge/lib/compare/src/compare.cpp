#include "giga_edge/lib/compare/compare.h"

#include "giga_edge/lib/compare/params.h"
#include "giga_edge/lib/enum/enum.h"
#include "giga_edge/lib/enum/enum_fwd.h"

#include <stdexcept>

GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(CompareOpType, "==", "!=", "<", "<=", ">", ">=");

namespace giga_edge {

bool CompareOp(float left, float right, CompareOpType compare_type) {
  switch (compare_type) {
    case CompareOpType::EQUAL:
      return left == right;
    case CompareOpType::NOT_EQUAL:
      return left != right;
    case CompareOpType::LESS:
      return left < right;
    case CompareOpType::LESS_EQUAL:
      return left <= right;
    case CompareOpType::GREATER:
      return left > right;
    case CompareOpType::GREATER_EQUAL:
      return left >= right;
  }

  throw std::invalid_argument(std::string("Invalid compare type ") + std::string(EnumToString(compare_type)));
}

} // namespace giga_edge
