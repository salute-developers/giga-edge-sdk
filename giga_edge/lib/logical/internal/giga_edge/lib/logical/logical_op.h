#pragma once

#include "logical_op_params.h"

namespace giga_edge {

bool LogicalOpBinary(bool left, bool right, LogicalOpType logical_type);
bool LogicalOpUnary(bool input, LogicalOpType logical_type);

} // namespace giga_edge