#include "giga_edge/modules/features/params.h"

#include "giga_edge/lib/common/overloaded.h"

#include <stdexcept>

namespace giga_edge {

void FeatureParams::InferValues() {
  std::visit(
    overloaded{
      [this](const asr::FeatureParams& asr_params) {
        num_mels = asr_params.num_filterbank_chans;
        int64_t frame_stride_samples_x1000 = asr_params.sampling_freq;
        frame_stride_samples_x1000 *= asr_params.frame_stride_ms;
        if (frame_stride_samples_x1000 % 1000 != 0) {
          throw std::runtime_error("ASR features: sampling_freq * frame_stride_ms should be divisible by 1000");
        }
        frame_stride_samples = frame_stride_samples_x1000 / 1000;
      },
      [this](const SpotterFeatureParams& spotter_params) {
        num_mels = spotter_params.num_mels;
        frame_stride_samples = spotter_params.frame_stride_samples;
      }
    },
    params
  );
}

} // namespace giga_edge