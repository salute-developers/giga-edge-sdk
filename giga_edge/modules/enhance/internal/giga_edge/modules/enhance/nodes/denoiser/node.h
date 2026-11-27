#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/enhance/chunk_buffer.h"
#include "giga_edge/modules/enhance/nodes/denoiser/params.h"

#include <deque>
#include <vector>

namespace giga_edge {

class DenoiserNode : public NodeBase {
public:
  DenoiserNode(const NodeBaseConfig& base_config, const DenoiserParams& denoiser_params);

  void ProcessOnce() override;

  void Reset() override;

  ~DenoiserNode() override;

  REGISTER_INPUT(enhance_denoiser, audio, Audio, std::vector<float>, "Incoming audio");
  REGISTER_OUTPUT(enhance_denoiser, audio, Audio, std::vector<float>, "Audio after denoiser processing");
  REGISTER_OPTIONAL_OUTPUT(enhance_denoiser, snr, Snr, float, "Stream SNR estimate (dB) after the processed chunk");

private:
  DenoiserParams params_;
  std::shared_ptr<enhance::Denoiser> denoiser_;
  std::unique_ptr<ChunkBuffer> processing_;
  std::unique_ptr<enhance::Context<float>, ContextDeleter<enhance::Denoiser>> denoiser_ctx_;
};

REGISTER_NODE(enhance_denoiser, DenoiserNode, DenoiserParams, "Audio denoiser processing");

} // namespace giga_edge
