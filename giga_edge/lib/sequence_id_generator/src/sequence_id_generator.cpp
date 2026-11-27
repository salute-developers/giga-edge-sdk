#include "giga_edge/lib/sequence_id_generator/sequence_id_generator.h"

namespace giga_edge {

SequenceIdGenerator::SequenceIdGenerator(const std::vector<std::string>& inputs, uint32_t next_seq_id)
  : next_seq_id_(next_seq_id) {
  for (const auto& input : inputs) {
    last_seq_ids_.emplace(input, next_seq_id_ - 1);
  }
}

uint32_t SequenceIdGenerator::NextSeqId() const {
  return next_seq_id_;
}

uint32_t SequenceIdGenerator::GenerateNextInputId(const std::string& input_id) {
  auto next_seq_id = ++last_seq_ids_.at(input_id);
  while (event_ids_.find(next_seq_id) != event_ids_.end()) {
    --event_ids_.at(next_seq_id);
    if (event_ids_.at(next_seq_id) == 0) {
      event_ids_.erase(next_seq_id);
    }
    next_seq_id = ++last_seq_ids_.at(input_id);
  }
  if (next_seq_id == next_seq_id_) {
    ++next_seq_id_;
  }

  return next_seq_id;
}

uint32_t SequenceIdGenerator::GenerateNextEventId() {
  auto id = next_seq_id_++;
  event_ids_[id] = last_seq_ids_.size();
  return id;
}

} // namespace giga_edge