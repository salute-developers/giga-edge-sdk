#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

#include <deque>
#include <vector>

namespace dsplib {
class FIRResampler;
} // namespace dsplib

namespace giga_edge {

class ResamplerNode : public NodeBase {
public:
  ResamplerNode(const NodeBaseConfig& base_config, const ResamplerParams& resampler_params);

  void ProcessOnce() override;

  void Reset() override;

  ~ResamplerNode() override;

  REGISTER_INPUT(resampler, audio, Audio, std::vector<float>, "Incoming audio");
  REGISTER_OUTPUT(resampler, audio, Audio, std::vector<float>, "Resampled audio");

private:
  ResamplerParams params_;
  std::unique_ptr<dsplib::FIRResampler> resampler_;
  std::vector<float> leftover_samples_;
};

REGISTER_NODE(resampler, ResamplerNode, ResamplerParams, "Audio resampler");

} // namespace giga_edge
