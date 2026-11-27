#pragma once

#include "giga_edge/modules/model/models/model_params.h"

#include <string>

namespace giga_edge {

struct RNNTModelNodeParams {
  struct Decoder : ModelParams {
    std::string path;
    std::vector<int> start_input;

    struct CallLimiter {
      int n_frames;
      int limit;
    } call_limiter;
  } decoder;

  struct Joint : ModelParams {
    enum class Format { TOKENS, LOGITS };

    std::string path;
    Format format = Format::TOKENS;

    int blank_id = -1;
    // Only for logits: subtracted from the blank logit before argmax, so the model swallows words less often.
    float blank_penalty = 0;
    int max_symbols_per_step = 0;

    // If the last `consequent_blanks_as_eos` outputs are blanks, converts the last output to eos:
    // output[tokens_output][eos_token_id] = 1.0.
    int consequent_blanks_as_eos;
    std::string tokens_output_name;
    int eos_token_id;
    bool is_interleaved_encoder_feature_copy_strategy;
  } joint;
};

} // namespace giga_edge