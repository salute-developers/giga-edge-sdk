#include "giga_edge/modules/llm/interpreter/engine/llama.cpp/engine.h"

#include "ggml-backend.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/modules/llm/special_text_escape.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_M_X64)
#include <immintrin.h>
#include <intrin.h>
#endif

namespace giga_edge {

namespace {

constexpr int32_t kLookupNgram = 4;

// llama.cpp for x64 is built with AVX2/FMA: without them the very first multiplication on the CPU dies with SIGILL.
bool CpuRunsLlamaBuild() {
#if defined(_M_X64)
  // CPUID rather than IsProcessorFeaturePresent: not every Windows version knows the PF_AVX2 flag, and it does not
  // check FMA.
  int info[4];
  __cpuid(info, 0);
  if (info[0] < 7) {
    return false;
  }
  __cpuid(info, 1);
  constexpr int kFmaOsxsaveAvx = (1 << 12) | (1 << 27) | (1 << 28);
  if ((info[2] & kFmaOsxsaveAvx) != kFmaOsxsaveAvx || (_xgetbv(0) & 6) != 6) {
    return false;
  }
  __cpuidex(info, 7, 0);
  return (info[1] & (1 << 5)) != 0;
#elif defined(__x86_64__)
  return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
#else
  return true;
#endif
}

std::optional<llama_flash_attn_type> ParseFlashAttentionType(const std::string& value) {
  std::string normalized = value;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });

  if (normalized == "auto") {
    return LLAMA_FLASH_ATTN_TYPE_AUTO;
  }
  if (normalized == "enabled" || normalized == "enable" || normalized == "true" || normalized == "1") {
    return LLAMA_FLASH_ATTN_TYPE_ENABLED;
  }
  if (normalized == "disabled" || normalized == "disable" || normalized == "false" || normalized == "0") {
    return LLAMA_FLASH_ATTN_TYPE_DISABLED;
  }
  return std::nullopt;
}

std::optional<ggml_type> ParseKvCacheType(const std::string& value) {
  if (value == "f16") {
    return GGML_TYPE_F16;
  }
  if (value == "q8_0") {
    return GGML_TYPE_Q8_0;
  }
  return std::nullopt;
}

// The model file on disk: the path and, if the file is inside an archive, its location there.
struct ModelFile {
  std::string path;
  std::optional<ArchiveMemberLocation> member;
};

ModelFile LocateModelFile(const std::string& path, const IDataReaderPtr& data_reader) {
  if (data_reader) {
    if (auto prefix = data_reader->GetPathPrefix()) {
      return {*prefix + path, std::nullopt};
    }
    if (auto member = data_reader->GetArchiveMemberLocation(path)) {
      return {member->archive_path, member};
    }
  }
  return {path, std::nullopt};
}

using FilePtr = std::unique_ptr<FILE, int (*)(FILE*)>;

// Opened at the start of the file data.
FilePtr OpenModelFile(const ModelFile& file) {
  FilePtr opened(std::fopen(file.path.c_str(), "rb"), &std::fclose);
  if (opened && file.member && std::fseek(opened.get(), static_cast<long>(file.member->offset), SEEK_SET) != 0) {
    return FilePtr(nullptr, &std::fclose);
  }
  return opened;
}

// The head is located in the same place as the model.
std::unique_ptr<LlamaPromptHead> LoadPromptHead(
  const LlmInterpreterParams& params, const IDataReaderPtr& data_reader, const llama_model* model, std::string& error
) {
  const auto file = LocateModelFile(params.prompt_head_path, data_reader);
  const int32_t n_embd = llama_model_n_embd(model);
  const int32_t n_layer = llama_model_n_layer(model);
  error = "cannot open " + params.prompt_head_path;
  const auto opened = OpenModelFile(file);
  return opened ? LlamaPromptHead::Load(opened.get(), n_embd, n_layer, error) : nullptr;
}

} // namespace

LlamaCppEngine::LlamaCppEngine() = default;

bool LlamaCppEngine::Initialize(const LlmInterpreterParams& params, const IDataReaderPtr& data_reader) {
  if (params.model_path.empty()) {
    return false;
  }
  if (params.n_ctx <= 0 || params.n_ctx > 32768) {
    return false;
  }
  if (params.n_batch <= 0 || params.n_batch > params.n_ctx) {
    return false;
  }
  if (params.top_k < 1 || params.top_k > 1000) {
    return false;
  }
  if (params.top_p <= 0.0f || params.top_p > 1.0f) {
    return false;
  }
  if (params.temp < 0.0f || params.temp > 2.0f) {
    return false;
  }
  if (params.n_threads < 0 || params.n_threads > 128) {
    return false;
  }
  if (params.n_threads_batch < 0 || params.n_threads_batch > 128) {
    return false;
  }
  const auto flash_attn_type = ParseFlashAttentionType(params.flash_attn);
  if (!flash_attn_type.has_value()) {
    return false;
  }
  const auto kv_cache_type = ParseKvCacheType(params.kv_cache_type);
  if (!kv_cache_type.has_value()) {
    return false;
  }
  if (!CpuRunsLlamaBuild()) {
    throw std::runtime_error("LLM is not supported on this CPU: AVX2 and FMA are required");
  }
  if (params.mtp_draft_max > 0 && params.lookup_draft_tokens > 0) {
    throw std::invalid_argument("LLM: mtp_draft_max and lookup_draft_tokens cannot be enabled together");
  }

  try {
    params_ = params;
    is_initialized_ = false;
    llama_backend_init();

    auto model_params = llama_model_default_params();
    model_params.n_gpu_layers = 99;
    on_gpu_ = llama_supports_gpu_offload();
#ifdef LLAMA_HAS_MTP_FUSED
    model_params.load_mtp = params.mtp_draft_max > 0;
#endif
    if (params.use_mmap) {
      model_params.load_mode = params.use_mlock ? LLAMA_LOAD_MODE_MMAP_MLOCK : LLAMA_LOAD_MODE_MMAP;
    } else {
      model_params.load_mode = params.use_mlock ? LLAMA_LOAD_MODE_MLOCK : LLAMA_LOAD_MODE_NONE;
    }

    const auto model_file = LocateModelFile(params.model_path, data_reader);
    if (model_file.member) {
      auto archive = OpenModelFile(model_file);
      if (!archive) {
        return false;
      }
      model_.reset(llama_model_load_from_file_ptr(archive.get(), model_params));
    } else {
      model_.reset(llama_model_load_from_file(model_file.path.c_str(), model_params));
    }
    if (!model_) {
      return false;
    }
    vocab_ = llama_model_get_vocab(model_.get());
    special_token_texts_.clear();
    for (llama_token token = 0; token < llama_vocab_n_tokens(vocab_); ++token) {
      if (IsSpecialToken(token) && *llama_vocab_get_text(vocab_, token) != '\0') {
        special_token_texts_.emplace_back(llama_vocab_get_text(vocab_, token));
      }
    }
    if (const auto error = CheckSpecialTexts(special_token_texts_); !error.empty()) {
      std::fprintf(stderr, "[llm] %s\n", error.c_str());
      return false;
    }

    auto ctx_params = llama_context_default_params();
    ctx_params.n_ctx = params.n_ctx;
    ctx_params.n_batch = params.n_batch;
    // 0: use all threads.
    unsigned int n_threads = (params.n_threads > 0) ? params.n_threads : std::thread::hardware_concurrency();
    unsigned int n_threads_batch = (params.n_threads_batch > 0) ? params.n_threads_batch : n_threads;
    ctx_params.n_threads = n_threads;
    ctx_params.n_threads_batch = n_threads_batch;
    n_threads_ = fitted_threads_ = static_cast<int>(n_threads);
    n_threads_batch_ = fitted_threads_batch_ = static_cast<int>(n_threads_batch);
    ctx_params.flash_attn_type = *flash_attn_type;
    ctx_params.type_k = ctx_params.type_v = *kv_cache_type;
    ctx_params.swa_full = params.swa_full;

    prompt_head_.reset();
    if (!params.prompt_head_path.empty()) {
      std::string error;
      prompt_head_ = LoadPromptHead(params, data_reader, model_.get(), error);
      if (!prompt_head_) {
        std::fprintf(stderr, "[llm] prompt head: %s\n", error.c_str());
        return false;
      }
      observed_rows_.assign(prompt_head_->Layers(), 0);
      ctx_params.cb_eval = &LlamaCppEngine::ObservePromptHead;
      ctx_params.cb_eval_user_data = this;
    }

    context_params_ = ctx_params;
    context_.reset(llama_init_from_model(model_.get(), ctx_params));
    if (!context_) {
      return false;
    }
    n_swa_ = llama_model_n_swa(model_.get());
    window_limited_ = !params.swa_full && n_swa_ > 0;
    window_checkpoints_.clear();

    n_ctx_ = llama_n_ctx(context_.get());
    n_batch_ = params.n_batch;
    EnableMtp();
    if (params.mtp_draft_max > 0 && mtp_draft_max_ == 0) {
      std::fprintf(stderr, "[llm] mtp_draft_max is set, but fused MTP is off (llama.cpp build or model): no drafts\n");
    }

    batch_ = LlamaBatchPtr(new llama_batch(llama_batch_init(n_batch_, 0, 1)));
    if (!batch_ || !batch_->token) {
      return false;
    }

    if (!BuildSampler(params.temp)) {
      return false;
    }

    is_initialized_ = true;

    eos_token_ = llama_vocab_eos(vocab_);
    extra_eog_tokens_.clear();
    for (const auto& text : params.eog_tokens) {
      const auto tokens = Encode(text, false);
      if (tokens.size() != 1) {
        std::fprintf(stderr, "[llm] eog_tokens: '%s' is not a single token\n", text.c_str());
        is_initialized_ = false;
        return false;
      }
      extra_eog_tokens_.push_back(tokens.front());
    }
    if (!params.stop_sequence.empty()) {
      stop_sequence_tokens_ = Encode(params.stop_sequence, false);
    }

    return true;
  } catch (...) {
    return false;
  }
}

void LlamaCppEngine::ClearContext() {
  if (!is_initialized_) {
    return;
  }

  llama_memory_clear(llama_get_memory(context_.get()), true);
  current_token_count_ = 0;
  if (prompt_head_) {
    prompt_head_->DropFrom(0);
  }
  should_continue_predicting_ = false;
  token_buffer_.clear();
  context_tokens_.clear();
  drafted_.clear();
  logits_index_ = -1;
  window_checkpoints_.clear();
}

std::optional<std::vector<LlmToken>> LlamaCppEngine::TokenizePrompt(const std::string& prompt) const {
  return TokenizePromptPart(prompt, true);
}

std::optional<std::vector<LlmToken>> LlamaCppEngine::TokenizePromptPart(
  const std::string& prompt, bool prepend_bos
) const {
  if (!is_initialized_) {
    return std::nullopt;
  }
  if (prompt.empty()) {
    return std::vector<LlmToken>{};
  }

  auto tokens = Encode(prompt, prepend_bos);
  return std::vector<LlmToken>(tokens.begin(), tokens.end());
}

bool LlamaCppEngine::IsTokenizationBoundary(LlmToken token) const {
  if (!is_initialized_) {
    return false;
  }
  // llama.cpp cuts the text at such tokens and tokenizes the pieces separately; a token with RSTRIP swallows the
  // spaces after it, so the text cannot be cut at it.
  const auto attr = llama_vocab_get_attr(vocab_, static_cast<llama_token>(token));
  return (attr & (LLAMA_TOKEN_ATTR_CONTROL | LLAMA_TOKEN_ATTR_USER_DEFINED | LLAMA_TOKEN_ATTR_UNKNOWN)) != 0
    && (attr & LLAMA_TOKEN_ATTR_RSTRIP) == 0;
}

const std::vector<std::string>& LlamaCppEngine::SpecialTokenTexts() const {
  return special_token_texts_;
}

bool LlamaCppEngine::IsSpecialToken(LlmToken token) const {
  return vocab_ != nullptr
    && (llama_vocab_get_attr(vocab_, static_cast<llama_token>(token))
        & (LLAMA_TOKEN_ATTR_CONTROL | LLAMA_TOKEN_ATTR_UNKNOWN))
    != 0;
}

bool LlamaCppEngine::AppendPromptTokens(const std::vector<LlmToken>& tokens, bool calculate_logits) {
  if (!is_initialized_) {
    return false;
  }

  token_buffer_.clear();
  std::vector<llama_token> llama_tokens(tokens.begin(), tokens.end());
  const bool appended = AppendTokensToContext(llama_tokens, calculate_logits);
  if (appended) {
    should_continue_predicting_ = true;
  }
  return appended;
}

bool LlamaCppEngine::BuildSampler(float temperature) {
  auto sampler_params = llama_sampler_chain_default_params();
  llama_sampler_ptr sampler(llama_sampler_chain_init(sampler_params));
  if (!sampler) {
    return false;
  }
  llama_sampler_chain_add(sampler.get(), llama_sampler_init_top_k(params_.top_k));
  llama_sampler_chain_add(sampler.get(), llama_sampler_init_top_p(params_.top_p, 1));
  llama_sampler_chain_add(sampler.get(), llama_sampler_init_temp(temperature));
  llama_sampler_chain_add(sampler.get(), llama_sampler_init_dist(params_.seed));
  sampler_ = std::move(sampler);
  sampling_temperature_ = temperature;
  return true;
}

bool LlamaCppEngine::SetSamplingTemperature(float temperature) {
  if (!is_initialized_) {
    return false;
  }
  if (temperature == sampling_temperature_) {
    return true;
  }
  return BuildSampler(temperature);
}

bool LlamaCppEngine::TruncateContextTo(size_t token_count) {
  if (!is_initialized_) {
    return false;
  }
  try {
    DropDrafted();
  } catch (const std::runtime_error&) {
    return false;
  }
  if (token_count > static_cast<size_t>(current_token_count_)) {
    return false;
  }
  if (token_count == static_cast<size_t>(current_token_count_)) {
    return true;
  }

  auto* memory = llama_get_memory(context_.get());
  const bool within_window = WindowHolds(token_count);
  if (!within_window && FindWindowCheckpoint(token_count) == window_checkpoints_.end()) {
    return false;
  }
  if (!llama_memory_seq_rm(memory, 0, static_cast<llama_pos>(token_count), -1)) {
    return false;
  }
  // Positions above the rollback point will be overwritten, the snapshots at them are no longer valid.
  window_checkpoints_.erase(
    std::remove_if(
      window_checkpoints_.begin(),
      window_checkpoints_.end(),
      [&](const WindowCheckpoint& checkpoint) {
        return checkpoint.token_count > token_count;
      }
    ),
    window_checkpoints_.end()
  );
  if (within_window) {
    SaveWindowCheckpoint(token_count);
  } else if (!RestoreWindowCheckpoint(token_count)) {
    // The window cache may be left half restored: only a reset helps.
    ClearContext();
    return false;
  }

  current_token_count_ = static_cast<int32_t>(token_count);
  context_tokens_.resize(token_count);
  if (prompt_head_) {
    prompt_head_->DropFrom(current_token_count_);
  }
  should_continue_predicting_ = false;
  calculate_logits_ = false;
  token_buffer_.clear();
  if (sampler_) {
    llama_sampler_reset(sampler_.get());
  }
  return true;
}

bool LlamaCppEngine::WindowHolds(size_t token_count) const {
  if (!window_limited_ || token_count == 0) {
    return true;
  }
  // A layer with a window looks at the last n_swa positions: they must remain in the window cache.
  const llama_pos pos_min = llama_memory_seq_pos_min(llama_get_memory(context_.get()), 0);
  return pos_min <= std::max<llama_pos>(0, static_cast<llama_pos>(token_count) - n_swa_);
}

std::vector<LlamaCppEngine::WindowCheckpoint>::iterator LlamaCppEngine::FindWindowCheckpoint(size_t token_count) {
  return std::find_if(window_checkpoints_.begin(), window_checkpoints_.end(), [&](const WindowCheckpoint& checkpoint) {
    return checkpoint.token_count == token_count;
  });
}

void LlamaCppEngine::PinRollbackPoint() {
  if (!is_initialized_) {
    return;
  }
  try {
    DropDrafted();
  } catch (const std::runtime_error&) {
    return;
  }
  SaveWindowCheckpoint(static_cast<size_t>(current_token_count_), true);
}

void LlamaCppEngine::SaveWindowCheckpoint(size_t token_count, bool pin) {
  constexpr std::ptrdiff_t max_checkpoints = 2; // not counting the pinned one
  if (!window_limited_ || token_count == 0) {
    return;
  }
  if (pin) {
    for (auto& checkpoint : window_checkpoints_) {
      checkpoint.pinned = false;
    }
  }
  // The snapshot at this point is still valid: the cache below it has not changed since.
  if (const auto existing = FindWindowCheckpoint(token_count); existing != window_checkpoints_.end()) {
    existing->pinned = existing->pinned || pin;
    std::rotate(existing, existing + 1, window_checkpoints_.end());
    return;
  }
  WindowCheckpoint checkpoint{token_count, {}, pin};
  checkpoint.data.resize(llama_state_seq_get_size_ext(context_.get(), 0, LLAMA_STATE_SEQ_FLAGS_PARTIAL_ONLY));
  const size_t written = llama_state_seq_get_data_ext(
    context_.get(), checkpoint.data.data(), checkpoint.data.size(), 0, LLAMA_STATE_SEQ_FLAGS_PARTIAL_ONLY
  );
  if (written == 0) {
    return;
  }
  checkpoint.data.resize(written);
  window_checkpoints_.push_back(std::move(checkpoint));
  const auto unpinned = [](const WindowCheckpoint& checkpoint) {
    return !checkpoint.pinned;
  };
  while (std::count_if(window_checkpoints_.begin(), window_checkpoints_.end(), unpinned) > max_checkpoints) {
    window_checkpoints_.erase(std::find_if(window_checkpoints_.begin(), window_checkpoints_.end(), unpinned));
  }
}

bool LlamaCppEngine::RestoreWindowCheckpoint(size_t token_count) {
  const auto checkpoint = FindWindowCheckpoint(token_count);
  if (checkpoint == window_checkpoints_.end()) {
    return false;
  }
  const size_t read = llama_state_seq_set_data_ext(
    context_.get(), checkpoint->data.data(), checkpoint->data.size(), 0, LLAMA_STATE_SEQ_FLAGS_PARTIAL_ONLY
  );
  if (read == 0) {
    return false;
  }
  std::rotate(checkpoint, checkpoint + 1, window_checkpoints_.end());
  return true;
}

std::optional<size_t> LlamaCppEngine::GetContextTokenCount() const {
  if (!is_initialized_) {
    return std::nullopt;
  }
  return static_cast<size_t>(current_token_count_) - drafted_.size();
}

size_t LlamaCppEngine::AcceptedDrafts() const {
  return accepted_drafts_;
}

std::vector<std::string> LlamaCppEngine::PromptHeadLabels() const {
  return prompt_head_ ? prompt_head_->Labels() : std::vector<std::string>{};
}

size_t LlamaCppEngine::PromptHeadTokens() const {
  return prompt_head_ ? prompt_head_->PoolTokens() : 0;
}

std::optional<std::vector<float>> LlamaCppEngine::PromptHeadProbabilities() const {
  return prompt_head_ ? prompt_head_->Predict(current_token_count_) : std::nullopt;
}

bool LlamaCppEngine::ObservePromptHead(ggml_tensor* tensor, bool ask, void* user_data) {
  auto& engine = *static_cast<LlamaCppEngine*>(user_data);
  const int index = engine.observe_prompt_ ? engine.prompt_head_->LayerIndex(tensor->name) : -1;
  if (ask) {
    return index >= 0;
  }
  if (index < 0) {
    return true;
  }
  const int32_t rows = static_cast<int32_t>(tensor->ne[1]);
  const int32_t first = engine.observed_pos_ + engine.observed_rows_[index];
  engine.observed_rows_[index] += rows;
  const int64_t n_embd = llama_model_n_embd(engine.model_.get());
  if (
    tensor->type != GGML_TYPE_F32 || tensor->ne[0] != n_embd || ggml_nrows(tensor) != rows
    || !ggml_is_contiguous(tensor)
  ) {
    engine.prompt_head_->DropFrom(first);
    return true;
  }
  // The head needs only the last positions: take the tail of the ubatch.
  const int32_t keep = std::min(rows, static_cast<int32_t>(engine.prompt_head_->PoolTokens()));
  engine.observed_buffer_.resize(static_cast<size_t>(keep * n_embd));
  const size_t row_bytes = static_cast<size_t>(n_embd) * sizeof(float);
  ggml_backend_tensor_get(
    tensor, engine.observed_buffer_.data(), static_cast<size_t>(rows - keep) * row_bytes, keep * row_bytes
  );
  engine.prompt_head_->StoreRows(index, first + rows - keep, engine.observed_buffer_.data(), keep);
  return true;
}

std::optional<size_t> LlamaCppEngine::GetContextCapacity() const {
  if (!is_initialized_) {
    return std::nullopt;
  }
  return static_cast<size_t>(n_ctx_);
}

std::optional<size_t> LlamaCppEngine::GetPromptTokenCount(const std::string& prompt) const {
  if (!is_initialized_) {
    return std::nullopt;
  }

  return CountTextTokens(prompt, true);
}

bool LlamaCppEngine::AppendTokensToContext(const std::vector<llama_token>& tokens, bool calculate_logits) {
  if (!is_initialized_) {
    return false;
  }
  if (tokens.empty()) {
    return true;
  }
  DropDrafted();

  calculate_logits_ = calculate_logits;

  const int part_token_count = static_cast<int>(tokens.size());
  if (n_ctx_ < current_token_count_ + part_token_count) {
    throw std::runtime_error("Not enough space in context for token part.");
  }

  for (int i = 0; i < part_token_count; i += n_batch_) {
    int batch_end = std::min(i + n_batch_, part_token_count);
    ClearBatch();

    for (int j = 0; j < batch_end - i; ++j) {
      const bool is_last_token_of_part = (i + j == part_token_count - 1);
      const bool is_logit = calculate_logits && is_last_token_of_part;
      AddToBatch(tokens[i + j], current_token_count_ + i + j, is_logit);
    }

    FitThreadsToAllowedCpus();
    observe_prompt_ = prompt_head_ != nullptr;
    observed_pos_ = current_token_count_ + i;
    std::fill(observed_rows_.begin(), observed_rows_.end(), 0);
    const bool decoded = llama_decode(context_.get(), *batch_) == 0;
    observe_prompt_ = false;
    if (!decoded) {
      RecreateContext();
      return false;
    }
  }

  current_token_count_ += part_token_count;
  context_tokens_.insert(context_tokens_.end(), tokens.begin(), tokens.end());
  logits_index_ = -1;
  draft_limit_ = std::min(params_.lookup_draft_tokens, on_gpu_ ? params_.lookup_draft_tokens : 1);
  return true;
}

LlmToken LlamaCppEngine::PredictNextToken() {
  const auto emitted = current_token_count_ - static_cast<int32_t>(drafted_.size());
  if (!is_initialized_ || !should_continue_predicting_ || emitted >= n_ctx_) {
    return eos_token_;
  }

  if (!calculate_logits_) {
    throw std::runtime_error("PredictNextToken failed due to calculate_logits is false.");
  }

  if (!batch_) {
    throw std::runtime_error("Batch is nullptr.");
  }

  const bool from_draft = !drafted_.empty();
  const int32_t row = logits_index_ >= 0 ? logits_index_ : batch_->n_tokens - 1;
  llama_token token = 0;
  if (from_draft) {
    token = drafted_.front();
    drafted_.pop_front();
  } else {
    token = llama_sampler_sample(sampler_.get(), context_.get(), row);
  }
  llama_sampler_accept(sampler_.get(), token);

  token_buffer_.push_back(token);

  const bool stop = IsEndOfTurn(token)
    || (!stop_sequence_tokens_.empty() && token_buffer_.size() >= stop_sequence_tokens_.size()
        && std::equal(stop_sequence_tokens_.rbegin(), stop_sequence_tokens_.rend(), token_buffer_.rbegin()));
  if (stop) {
    if (from_draft) {
      // A stop from the draft is already in the KV: remove it as if it had never been decoded.
      const auto stop_pos = current_token_count_ - static_cast<int32_t>(drafted_.size()) - 1;
      drafted_.clear();
      ForgetFrom(stop_pos);
    }
    should_continue_predicting_ = false;
    return eos_token_;
  }
  if (from_draft) {
    return token;
  }

  const auto draft = mtp_draft_max_ > 0 ? MtpDraft(row) : LookupDraft(token);
  ClearBatch();
  AddToBatch(token, current_token_count_);
  for (size_t i = 0; i < draft.size(); ++i) {
    AddToBatch(draft[i], current_token_count_ + 1 + static_cast<llama_pos>(i));
  }

  FitThreadsToAllowedCpus();
  if (llama_decode(context_.get(), *batch_) != 0) {
    RecreateContext();
    throw std::runtime_error("llama_decode failed during generation.");
  }

  current_token_count_ += 1 + static_cast<int32_t>(draft.size());
  context_tokens_.push_back(token);
  context_tokens_.insert(context_tokens_.end(), draft.begin(), draft.end());

  const int32_t n_vocab = llama_vocab_n_tokens(vocab_);
  size_t accepted = 0;
  while (accepted < draft.size()) {
    const float* logits = llama_get_logits_ith(context_.get(), static_cast<int32_t>(accepted));
    if (std::max_element(logits, logits + n_vocab) - logits != draft[accepted]) {
      break;
    }
    ++accepted;
  }
  ForgetFrom(current_token_count_ - static_cast<int32_t>(draft.size() - accepted));
  drafted_.assign(draft.begin(), draft.begin() + static_cast<std::ptrdiff_t>(accepted));
  logits_index_ = static_cast<int32_t>(accepted);
  accepted_drafts_ += accepted;
  if (mtp_draft_max_ == 0 && !draft.empty()) {
    // On a GPU medium batches are no cheaper than long ones: one token after a miss, the maximum right after a hit;
    // on a CPU the cost grows with the batch length: grow from one token by doubling and cut by halving.
    const bool hit = accepted == draft.size();
    const int32_t top = params_.lookup_draft_tokens;
    draft_limit_ = on_gpu_ ? (hit ? top : 1) : (hit ? std::min(2 * draft_limit_, top) : std::max(1, draft_limit_ / 2));
  }
  return token;
}

bool LlamaCppEngine::IsEndOfTurn(llama_token token) const {
  return llama_vocab_is_eog(vocab_, token)
    || std::find(extra_eog_tokens_.begin(), extra_eog_tokens_.end(), token) != extra_eog_tokens_.end();
}

std::vector<llama_token> LlamaCppEngine::LookupDraft(llama_token last) const {
  const int32_t limit = std::min({draft_limit_, n_batch_ - 1, n_ctx_ - current_token_count_ - 1});
  if (limit <= 0 || sampling_temperature_ > 0.0f) {
    return {};
  }
  // The latest past occurrence of the last n tokens; what follows it is the draft.
  const auto& seen = context_tokens_;
  const auto size = static_cast<int32_t>(seen.size());
  for (int32_t n = std::min(kLookupNgram, size); n >= 1; --n) {
    for (int32_t start = size - n - 1; start >= 0; --start) {
      if (seen[start + n - 1] == last && std::equal(seen.end() - (n - 1), seen.end(), seen.begin() + start)) {
        const int32_t from = start + n;
        return {seen.begin() + from, seen.begin() + std::min(size, from + limit)};
      }
    }
  }
  return {};
}

// The token was taken from the row `row` by argmax: the MTP blocks of the same row predicted what comes after it.
std::vector<llama_token> LlamaCppEngine::MtpDraft(int32_t row) const {
  const int32_t limit = std::min(mtp_draft_max_, n_ctx_ - current_token_count_ - 1);
  if (limit <= 0 || sampling_temperature_ > 0.0f) {
    return {};
  }
  std::vector<llama_token> draft(static_cast<size_t>(limit));
#ifdef LLAMA_HAS_MTP_FUSED
  draft.resize(static_cast<size_t>(std::max(0, llama_get_mtp_drafts_ith(context_.get(), row, draft.data(), limit))));
#else
  draft.clear();
#endif
  return draft;
}

void LlamaCppEngine::EnableMtp() {
  mtp_draft_max_ = 0;
#ifdef LLAMA_HAS_MTP_FUSED
  // The token and the drafts go in one batch; llama may refuse (no MTP layers, flash attention and the like).
  if (params_.mtp_draft_max > 0) {
    llama_set_mtp_fused(context_.get(), std::min(params_.mtp_draft_max, n_batch_ - 1));
    mtp_draft_max_ = llama_get_mtp_fused(context_.get());
  }
#endif
}

// Android takes cores away from a process on the fly (heat, low battery, a background cpuset). ggml threads wait
// for each other at barriers, and the extra ones on busy cores stretch a turn tens of times: keep no more threads
// than the allowed cores.
void LlamaCppEngine::FitThreadsToAllowedCpus() {
  const int cpus = AllowedCpuCount();
  if (cpus <= 0) {
    return;
  }
  const int threads = std::min(n_threads_, cpus);
  const int threads_batch = std::min(n_threads_batch_, cpus);
  if (threads != fitted_threads_ || threads_batch != fitted_threads_batch_) {
    llama_set_n_threads(context_.get(), threads, threads_batch);
    fitted_threads_ = threads;
    fitted_threads_batch_ = threads_batch;
  }
}

void LlamaCppEngine::RecreateContext() {
  context_.reset();
  context_.reset(llama_init_from_model(model_.get(), context_params_));
  is_initialized_ = context_ != nullptr;
  if (is_initialized_) {
    EnableMtp();
  }
  fitted_threads_ = n_threads_;
  fitted_threads_batch_ = n_threads_batch_;
  current_token_count_ = 0;
  if (prompt_head_) {
    prompt_head_->DropFrom(0);
  }
  should_continue_predicting_ = false;
  token_buffer_.clear();
  context_tokens_.clear();
  drafted_.clear();
  logits_index_ = -1;
  window_checkpoints_.clear();
  if (sampler_) {
    llama_sampler_reset(sampler_.get());
  }
}

void LlamaCppEngine::DropDrafted() {
  if (drafted_.empty()) {
    return;
  }
  const auto pos = current_token_count_ - static_cast<int32_t>(drafted_.size());
  drafted_.clear();
  ForgetFrom(pos);
}

void LlamaCppEngine::ForgetFrom(int32_t pos) {
  if (pos >= current_token_count_) {
    return;
  }
  if (!llama_memory_seq_rm(llama_get_memory(context_.get()), 0, pos, -1)) {
    // The model memory cannot roll back partially (recurrent layers): a KV out of sync is worse than an error.
    should_continue_predicting_ = false;
    throw std::runtime_error("KV cache cannot drop draft tokens.");
  }
  current_token_count_ = pos;
  context_tokens_.resize(static_cast<size_t>(pos));
}

std::string LlamaCppEngine::Decode(LlmToken token) {
  if (!is_initialized_) {
    return "";
  }

  std::vector<char> buffer(16, 0);
  int len = llama_token_to_piece(vocab_, static_cast<llama_token>(token), buffer.data(), buffer.size(), 0, true);

  if (len < 0) {
    size_t required_size = static_cast<size_t>(-len);
    // A piece over 1 MB means a broken vocabulary: return an empty piece rather than allocate it.
    if (required_size > 1024 * 1024) {
      return "";
    }
    buffer.resize(required_size);
    len = llama_token_to_piece(vocab_, static_cast<llama_token>(token), buffer.data(), buffer.size(), 0, true);
    if (len < 0) {
      return "";
    }
  }

  return std::string(buffer.data(), len);
}

void LlamaCppEngine::StopGeneration() {
  should_continue_predicting_ = false;
}

LlmToken LlamaCppEngine::GetEosToken() const {
  return eos_token_;
}

bool LlamaCppEngine::IsReady() const {
  return is_initialized_;
}

void LlamaCppEngine::AddToBatch(llama_token token, llama_pos pos, bool is_logit) {
  if (!batch_) {
    throw std::runtime_error("Batch is nullptr.");
  }

  int i = batch_->n_tokens;
  batch_->token[i] = token;
  batch_->pos[i] = pos;
  batch_->n_seq_id[i] = 1;
  batch_->seq_id[i][0] = 0;
  batch_->logits[i] = is_logit ? 1 : 0;
  batch_->n_tokens++;
}

void LlamaCppEngine::ClearBatch() {
  if (!batch_) {
    throw std::runtime_error("Batch is nullptr.");
  }

  batch_->n_tokens = 0;
}

std::vector<llama_token> LlamaCppEngine::Encode(const std::string& text, bool prepend_bos) const {
  if (!is_initialized_) {
    return {};
  }

  const int text_len = static_cast<int>(text.length());
  int buf_cap = text_len * 2 + 32;
  if (buf_cap < 8) {
    buf_cap = 8;
  }

  std::vector<llama_token> result(static_cast<size_t>(buf_cap));
  int n_tokens = llama_tokenize(
    vocab_,
    text.c_str(),
    text_len,
    result.data(),
    buf_cap,
    false, // add_special: do not inject BOS/EOS (EOS at end breaks generation after assistant prefix)
    true
  ); // parse_special: tokenize <|...|> and other control tokens

  if (n_tokens < 0) {
    const int needed = -n_tokens;
    result.resize(static_cast<size_t>(needed));
    n_tokens = llama_tokenize(vocab_, text.c_str(), text_len, result.data(), needed, false, true);
    if (n_tokens < 0) {
      return {};
    }
  }

  result.resize(static_cast<size_t>(n_tokens));

  if (prepend_bos) {
    const llama_token bos = llama_vocab_bos(vocab_);
    if (bos >= 0) {
      result.insert(result.begin(), bos);
    }
  }

  return result;
}

std::optional<size_t> LlamaCppEngine::CountTextTokens(const std::string& text, bool prepend_bos) const {
  if (!is_initialized_) {
    return std::nullopt;
  }
  if (text.empty()) {
    return size_t{0};
  }
  return Encode(text, prepend_bos).size();
}

} // namespace giga_edge
