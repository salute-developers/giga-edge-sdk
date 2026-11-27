#pragma once

#include <string>
#include <vector>

namespace giga_edge {

struct LlmInterpreterParams {
  std::string model_path;
  std::string stop_sequence;
  // End of turn beyond the model's own set (EOS, EOT and the like from the vocabulary): token texts, each exactly
  // one token.
  std::vector<std::string> eog_tokens;
  int32_t seed = -1; // -1 for a random seed
  int32_t top_k = 40;
  float top_p = 0.95f;
  float temp = 0.8f;
  int32_t n_ctx = 2048;
  int32_t n_batch = 512;
  int32_t n_threads = 0;             // 0: use all threads
  int32_t n_threads_batch = 0;       // 0: use n_threads for batch/prefill
  std::string flash_attn = "auto";   // auto, enabled, disabled
  std::string kv_cache_type = "f16"; // f16, q8_0 (q8_0 takes half the memory, needs flash_attn)
  bool use_mmap = true;
  // Weights under mmap are evictable, and under memory pressure they get dropped between requests.
  bool use_mlock = false;
  // A draft from the text already in the context (prompt lookup), 0 is off; only with temp 0.
  int32_t lookup_draft_tokens = 0;
  // The cache of sliding-window layers covers the whole context. false: only the window, which takes less memory,
  // but a rollback deeper than the window is possible only to a window snapshot, otherwise the context is recomputed.
  bool swa_full = true;
  // MTP drafts per generation step (0: no speculation). Greedy sampling and a model with MTP layers only: a step
  // verifies the token and the drafts in one batch, the accepted ones are returned without new runs.
  int32_t mtp_draft_max = 0;
  // Empty: there is no head, the llama.cpp graph is computed without an observer.
  std::string prompt_head_path;
};

} // namespace giga_edge
