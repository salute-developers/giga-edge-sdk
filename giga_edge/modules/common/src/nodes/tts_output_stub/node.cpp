#include "giga_edge/modules/common/nodes/tts_output_stub/node.h"

#include <utility>

namespace giga_edge {

TtsOutputStubNode::TtsOutputStubNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void TtsOutputStubNode::ProcessOnce() {
  // A phrase ends at once and without sound; after a reset, so does the phrase dropped by the real synthesis.
  const bool dropped = std::exchange(close_dropped_, false);
  const bool eos = !GetText().empty() || dropped;
  PushAudioChunk({});
  PushIsEos(eos);
}

void TtsOutputStubNode::Reset() {
  close_dropped_ = true;
}

} // namespace giga_edge
