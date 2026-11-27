#include "giga_edge/modules/common/nodes/accumulate_buffer/node.h"

namespace giga_edge {

AccumulateBufferNode::AccumulateBufferNode(const NodeBaseConfig& base_config, const AccumulateBufferNodeParams& params)
  : NodeBase(base_config)
  , params_(params) {}

void AccumulateBufferNode::ProcessOnce() {
  for (auto operation : params_.operations_priority) {
    switch (operation) {
      case AccumulateBufferNodeParams::Operation::INPUTS:
        data_.insert(data_.end(), GetInput().begin(), GetInput().end());
        while (data_.size() > params_.max_size) {
          data_.pop_front();
        }
        break;
      case AccumulateBufferNodeParams::Operation::DROP:
        if (GetDrop().has_value() && GetDrop().value().get()) {
          data_.clear();
        }
        break;
      case AccumulateBufferNodeParams::Operation::PUSH:
        if (!GetPush().has_value() || GetPush().value()) {
          PushOutput({data_.begin(), data_.end()});
          if (params_.clear_after_push) {
            data_.clear();
          }
        } else {
          PushOutput({});
        }
        break;
    }
  }
}

void AccumulateBufferNode::Reset() {
  data_.clear();
}

} // namespace giga_edge
