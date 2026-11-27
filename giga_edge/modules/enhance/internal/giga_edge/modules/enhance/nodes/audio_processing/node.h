#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/enhance/library.h"
#include "giga_edge/modules/enhance/nodes/audio_processing/params.h"

namespace giga_edge {

class AudioProcessingNode : public NodeBase {
public:
  AudioProcessingNode(const NodeBaseConfig& base_config, const AudioProcessingParams& denoiser_params);
  ~AudioProcessingNode() override;

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_INPUT(enhance_audio_processing, audio, Audio, std::vector<float>, "Incoming audio");
  REGISTER_OUTPUT(enhance_audio_processing, audio, Audio, std::vector<float>, "Audio after audio processing");

private:
  std::unique_ptr<enhance::AudioProcessor> audio_process_;
};

REGISTER_NODE(enhance_audio_processing, AudioProcessingNode, AudioProcessingParams, "Audio processing node");

} // namespace giga_edge
