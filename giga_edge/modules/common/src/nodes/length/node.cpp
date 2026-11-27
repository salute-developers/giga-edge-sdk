#include "giga_edge/modules/common/nodes/length/node.h"

namespace giga_edge {
namespace {

size_t CountUtf8Codepoints(const std::string& s) {
  size_t count = 0;
  for (unsigned char c : s) {
    count += (c & 0xC0) != 0x80;
  }
  return count;
}

} // namespace

LengthNode::LengthNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {
  if (base_config.inputs.size() != 1) {
    throw std::runtime_error("LengthNode: exactly one of data inputs should be present");
  }
}

void LengthNode::ProcessOnce() {
  if (const auto& input = GetFloatVector(); input.has_value()) {
    PushLength(static_cast<float>(input.value().get().size()));
  }

  if (const auto& input = GetString(); input.has_value()) {
    PushLength(static_cast<float>(CountUtf8Codepoints(input.value().get())));
  }
}

} // namespace giga_edge
