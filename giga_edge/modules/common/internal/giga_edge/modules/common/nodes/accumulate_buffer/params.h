#pragma once

#include <cstddef>
#include <vector>

namespace giga_edge {

struct AccumulateBufferNodeParams {
  enum class Operation { INPUTS, DROP, PUSH };

  long long max_size = 10'000'000'000;
  bool clear_after_push = true;
  std::vector<Operation> operations_priority = {Operation::INPUTS, Operation::DROP, Operation::PUSH};
};

} // namespace giga_edge
