#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/synthesis_text/events.h"

namespace giga_edge {

class SynthesisTextBufferNode : public NodeBase {
public:
  SynthesisTextBufferNode(
    const NodeBaseConfig& base_config, const SynthesisTextBufferParams& synthesis_text_buffer_params
  );

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_INPUT(
    synthesis_text_buffer,
    allow_output,
    AllowOutput,
    bool,
    "Flag for determining whether to output the saved text.\
                                                                          Otherwise, an empty string is returned."
  );

  REGISTER_OUTPUT(synthesis_text_buffer, synthesis_text, SynthesisText, std::string, "Saved sythesis string.");

private:
  void ProcessEventImpl(const std::any& event) override;

  void ProcessSetSynthesisText(const SetSynthesisTextEvent& event);
  void ProcessResetSynthesisText(const ResetSynthesisTextEvent& event);

private:
  std::string synthesis_text_;
  SynthesisTextBufferParams synthesis_text_buffer_params_;
  size_t num_frames_after_reset_synthesis_text_;
};

REGISTER_NODE(
  synthesis_text_buffer, SynthesisTextBufferNode, SynthesisTextBufferParams, "A node for storing synthesized text."
);

} // namespace giga_edge
