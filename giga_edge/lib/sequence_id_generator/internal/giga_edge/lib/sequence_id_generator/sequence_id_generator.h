#pragma once

#include "giga_edge/lib/common/non_copyable.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace giga_edge {

class SequenceIdGenerator : public NonCopyable {
public:
  explicit SequenceIdGenerator(const std::vector<std::string>& inputs, uint32_t next_seq_id = 0);

  uint32_t NextSeqId() const;
  uint32_t GenerateNextInputId(const std::string& input_id);
  uint32_t GenerateNextEventId();

private:
  uint32_t next_seq_id_;
  std::unordered_map<std::string, uint32_t> last_seq_ids_;
  std::unordered_map<uint32_t, uint32_t> event_ids_;
};

} // namespace giga_edge
