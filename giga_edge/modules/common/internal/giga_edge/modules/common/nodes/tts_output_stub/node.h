#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class TtsOutputStubNode : public NodeBase {
public:
  explicit TtsOutputStubNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_INPUT(tts_output_stub, text, Text, std::string, "Text");
  REGISTER_BATCH_OUTPUT(tts_output_stub, audio_chunk, AudioChunk, std::vector<float>, "Empty PCM chunk");
  REGISTER_OUTPUT(
    tts_output_stub,
    is_eos,
    IsEos,
    bool,
    "End of empty synthesis: on a tick with text and once after a reset (closes the phrase left by the real TTS)"
  );

private:
  bool close_dropped_ = false;
};

REGISTER_NODE(tts_output_stub, TtsOutputStubNode, "Stub, that generates empty TTS output");

} // namespace giga_edge
