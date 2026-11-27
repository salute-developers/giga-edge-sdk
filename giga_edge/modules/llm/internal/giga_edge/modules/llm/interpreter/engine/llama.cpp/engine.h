#pragma once

#include "llama-cpp.h"
#include "llama.h"

#include "giga_edge/modules/llm/interpreter/engine/llama.cpp/prompt_head.h"
#include "giga_edge/modules/llm/interpreter/interpreter.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace giga_edge {

// RAII wrappers for llama.cpp resources.
struct LlamaBatchDeleter {
  void operator()(llama_batch* batch) const {
    if (batch) {
      llama_batch_free(*batch);
      delete batch; // llama_batch_free frees the contents but not the heap object itself
    }
  }
};

using LlamaBatchPtr = std::unique_ptr<llama_batch, LlamaBatchDeleter>;

class LlamaCppEngine : public ILlmInterpreter {
public:
  // This class is not thread-safe: all methods must be called from one thread.
  LlamaCppEngine();
  ~LlamaCppEngine() override = default;

  LlamaCppEngine(const LlamaCppEngine&) = delete;
  LlamaCppEngine& operator=(const LlamaCppEngine&) = delete;

  bool Initialize(const LlmInterpreterParams& params, const IDataReaderPtr& data_reader) override;
  bool SetSamplingTemperature(float temperature) override;
  void ClearContext() override;
  std::optional<std::vector<LlmToken>> TokenizePrompt(const std::string& prompt) const override;
  std::optional<std::vector<LlmToken>> TokenizePromptPart(const std::string& prompt, bool prepend_bos) const override;
  bool IsTokenizationBoundary(LlmToken token) const override;
  const std::vector<std::string>& SpecialTokenTexts() const override;
  bool IsSpecialToken(LlmToken token) const override;
  bool AppendPromptTokens(const std::vector<LlmToken>& tokens, bool calculate_logits = true) override;
  bool TruncateContextTo(size_t token_count) override;
  void PinRollbackPoint() override;
  size_t AcceptedDrafts() const override;
  std::vector<std::string> PromptHeadLabels() const override;
  size_t PromptHeadTokens() const override;
  std::optional<std::vector<float>> PromptHeadProbabilities() const override;
  std::optional<size_t> GetContextTokenCount() const override;
  std::optional<size_t> GetContextCapacity() const override;
  LlmToken PredictNextToken() override;
  std::string Decode(LlmToken token) override;
  void StopGeneration() override;
  LlmToken GetEosToken() const override;
  bool IsReady() const override;

  std::optional<size_t> GetPromptTokenCount(const std::string& prompt) const override;

private:
  void AddToBatch(llama_token token, llama_pos pos, bool is_logit = true);
  void ClearBatch();
  // Second arg: prepend BOS via llama_vocab_bos. Do not use llama_tokenize(add_special=true) here,
  // it also appends EOS for many models and breaks manual chat prompts.
  std::vector<llama_token> Encode(const std::string& text, bool prepend_bos = true) const;
  std::optional<size_t> CountTextTokens(const std::string& text, bool prepend_bos) const;

  bool AppendTokensToContext(const std::vector<llama_token>& tokens, bool calculate_logits = true);
  // End of turn: the model's own set (llama_vocab_is_eog) and eog_tokens from the config.
  bool IsEndOfTurn(llama_token token) const;
  std::vector<llama_token> LookupDraft(llama_token last) const;
  std::vector<llama_token> MtpDraft(int32_t row) const;
  void EnableMtp();
  void DropDrafted();
  void ForgetFrom(int32_t pos);
  void FitThreadsToAllowedCpus();
  void RecreateContext();
  static bool ObservePromptHead(ggml_tensor* tensor, bool ask, void* user_data);

  // The window cache (swa_full: false) remembers only the last positions. Window snapshots at rollback points
  // allow a return to them deeper than the window without recomputing the whole context.
  struct WindowCheckpoint {
    size_t token_count = 0;
    std::vector<uint8_t> data;
    bool pinned = false;
  };
  bool WindowHolds(size_t token_count) const;
  std::vector<WindowCheckpoint>::iterator FindWindowCheckpoint(size_t token_count);
  void SaveWindowCheckpoint(size_t token_count, bool pin = false);
  bool RestoreWindowCheckpoint(size_t token_count);

  LlmInterpreterParams params_;
  bool is_initialized_ = false;

  llama_model_ptr model_;
  const llama_vocab* vocab_ = nullptr;
  llama_context_ptr context_;
  llama_context_params context_params_{};
  LlamaBatchPtr batch_;

  llama_sampler_ptr sampler_;
  float sampling_temperature_ = 0.0f;
  bool BuildSampler(float temperature);

  bool should_continue_predicting_ = false;
  int32_t current_token_count_ = 0;
  int32_t n_ctx_ = 0;
  int32_t n_batch_ = 0;
  int n_threads_ = 0;
  int n_threads_batch_ = 0;
  int fitted_threads_ = 0;
  int fitted_threads_batch_ = 0;
  bool calculate_logits_ = false;

  llama_token eos_token_;
  std::vector<llama_token> extra_eog_tokens_;
  std::vector<std::string> special_token_texts_;
  std::vector<llama_token> stop_sequence_tokens_;
  std::vector<llama_token> token_buffer_;

  std::vector<llama_token> context_tokens_;
  // Accepted draft tokens: already in the KV, not yet returned to the caller.
  std::deque<llama_token> drafted_;
  int32_t logits_index_ = -1;
  int32_t draft_limit_ = 0;
  // MTP drafts per step: 0 means MTP is off (not set or llama.cpp refused).
  int32_t mtp_draft_max_ = 0;
  size_t accepted_drafts_ = 0;
  bool on_gpu_ = false;
  bool window_limited_ = false;
  int32_t n_swa_ = 0;
  std::vector<WindowCheckpoint> window_checkpoints_;

  std::unique_ptr<LlamaPromptHead> prompt_head_;
  // The position of the first row of a decode and how many rows of each head layer have arrived (ubatches come in
  // order).
  bool observe_prompt_ = false;
  int32_t observed_pos_ = 0;
  std::vector<int32_t> observed_rows_;
  std::vector<float> observed_buffer_;
};

} // namespace giga_edge
