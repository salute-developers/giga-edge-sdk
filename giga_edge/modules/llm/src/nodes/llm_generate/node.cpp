#include "giga_edge/modules/llm/nodes/llm_generate/node.h"

#include "giga_edge/lib/environment/environment.h"
#include "giga_edge/modules/assistant_core/json.h"
#include "giga_edge/modules/assistant_core/text.h"
#include "giga_edge/modules/llm/special_text_escape.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace giga_edge {
namespace {

const char* const kCallSourceMismatch = "Tool call parser source does not match the raw text";

double MsBetween(std::chrono::steady_clock::time_point from, std::chrono::steady_clock::time_point to) {
  return std::chrono::duration<double, std::milli>(to - from).count();
}

bool IsUserTurn(const AssistantRequest& request) {
  return request.kind == AssistantRequestKind::GENERATE && !request.messages.empty()
    && request.messages.back().role == MessageRole::USER;
}

bool Declines(
  const PromptHeadParams& head,
  const std::vector<std::string>& labels,
  const std::vector<float>& probabilities,
  const std::string& top
) {
  const auto& decline = head.decline_labels;
  if (decline.empty()) {
    return false;
  }
  const auto declining = [&decline](const std::string& label) {
    return std::find(decline.begin(), decline.end(), label) != decline.end();
  };
  switch (head.decision) {
    case PromptHeadDecision::ARGMAX:
      return declining(top);
    case PromptHeadDecision::THRESHOLD: {
      float keep = 0.0f;
      for (size_t i = 0; i < labels.size() && i < probabilities.size(); ++i) {
        keep += declining(labels[i]) ? 0.0f : probabilities[i];
      }
      return keep < head.threshold;
    }
  }
  return false;
}

} // namespace

LlmGenerate::LlmGenerate(
  const NodeBaseConfig& base_config, const IDataReaderPtr& data_reader, const LlmGenerateParams& params
)
  : NodeBase(base_config)
  , params_(params)
  , template_(params.prompt_template)
  , planner_(
      template_,
      [this](const std::string& text, bool prepend_bos) {
        return interpreter_->TokenizePromptPart(text, prepend_bos);
      },
      [this](LlmToken token) {
        return interpreter_->IsTokenizationBoundary(token);
      },
      [this]() -> const std::vector<std::string>& {
        return interpreter_->SpecialTokenTexts();
      }
    )
  , debug_(
      params.local_debug_enabled,
      params.debug_dump_prompt,
      params.llm_interpreter_params.mtp_draft_max > 0 ? DraftSource::MTP : DraftSource::PROMPT_LOOKUP
    ) {
  if (!params_.async_thread_pool.empty()) {
    if (!base_config.environments) {
      throw std::runtime_error("llm_generate: async inference requires model environments");
    }
    const auto& environments = *base_config.environments;
    const auto it = environments.find(params_.async_thread_pool);
    const auto pool = it == environments.end() ? nullptr : std::dynamic_pointer_cast<ThreadPoolEnvironment>(it->second);
    if (!pool || !pool->thread_pool) {
      throw std::runtime_error("llm_generate: no thread pool environment '" + params_.async_thread_pool + "'");
    }
    worker_pool_ = pool->thread_pool;
  }

  ConfigureLlamaLogging();
  interpreter_ = LlmInterpreterFactory::CreateInterpreter(params_.backend_type);
  if (!interpreter_ || !interpreter_->Initialize(params_.llm_interpreter_params, data_reader)) {
    throw std::runtime_error("Failed to initialize LLM interpreter");
  }
  if (!params_.tool_call_parser.empty()) {
    parser_ = ToolCallParserRegistry::Create(params_.tool_call_parser, params_.tool_call_parser_params);
  }
  filter_ = StreamToolCallFilter(parser_);
  const auto labels = interpreter_->PromptHeadLabels();
  for (const auto& label : params_.prompt_head.decline_labels) {
    if (std::find(labels.begin(), labels.end(), label) == labels.end()) {
      throw std::runtime_error("llm_generate: prompt_head has no label '" + label + "'");
    }
  }
  // outputs_data_ lives in the graph tick, while chunks are also collected by the LLM thread.
  with_report_ = Outputs().count("llm_report") != 0;
}

LlmGenerate::~LlmGenerate() {
  StopWorker();
}

void LlmGenerate::Reset() {
  StopWorker();
  queued_warm_.reset();
  {
    std::lock_guard lock(emitted_mutex_);
    emitted_.clear();
  }
  working_ = false;
  interpreter_->ClearContext();
  kv_.Clear();
  pinned_tokens_ = 0;
  filter_.Reset();
  state_ = State::IDLE;
  active_request_id_.clear();
  last_request_id_.clear();
  active_generate_ = false;
  deferred_.reset();
  pending_text_.clear();
  pending_tokens_.clear();
  pending_ends_.clear();
  pending_offset_ = 0;
  pending_chunks_ = 0;
  reused_tokens_ = 0;
  prefilled_tokens_ = 0;
  generated_count_ = 0;
  generated_text_.clear();
  generated_ends_.clear();
  generated_tokens_.clear();
  classify_prompt_ = false;
  allow_decline_ = false;
  prompt_label_.clear();
  prompt_probabilities_.clear();
  debug_.Reset();
}

void LlmGenerate::ProcessOnce() {
  if (IsAsync()) {
    ProcessOnceAsync();
    return;
  }

  const auto& request = GetAssistantRequest();
  if (const auto reset = GetDialogueReset(); reset.has_value() && reset->get()) {
    Reset();
    PushIdle();
    FinishLoop();
    return;
  }

  if (const auto cancel = GetCancel(); cancel.has_value() && cancel->get() && state_ != State::IDLE) {
    CancelGeneration();
    ProcessRequest(request);
    if (state_ == State::IDLE) {
      FinishLoop();
    }
    return;
  }

  if (state_ != State::IDLE) {
    const bool new_request = IsNewRequest(request, active_request_id_, active_generate_);
    if (params_.on_new_request == OnNewRequestPolicy::REPLACE && new_request) {
      CancelGeneration();
      Start(request);
      return;
    }
    if (new_request && !active_generate_ && request.kind == AssistantRequestKind::GENERATE) {
      // A request during a warm-up is not lost: it starts as soon as the warm-up finishes writing the KV.
      deferred_ = request;
    }
    PushIdle();
    StartLoop();
    return;
  }

  ProcessRequest(request);
}

void LlmGenerate::ProcessOnceAsync() {
  const auto& request = GetAssistantRequest();
  // There is nothing to cancel in a warm-up: cancel does not touch it, the tick does not wait for it.
  const bool warming = IsWorkerBusy() && !launched_generate_;
  // Like a preemption by a new request, only without one: the KV and the history are intact, when idle nothing
  // happens.
  if (
    const auto cancel = GetCancel();
    cancel.has_value() && cancel->get() && !warming && (IsWorkerBusy() || state_ != State::IDLE)
  ) {
    StopWorker();
    CancelGeneration();
    StopWorking();
  }
  if (const auto reset = GetDialogueReset(); reset.has_value() && reset->get()) {
    Reset();
    PushIdle();
  } else if (IsWorkerBusy()) {
    if (params_.on_new_request == OnNewRequestPolicy::REPLACE) {
      if (request.kind == AssistantRequestKind::WARM && request.request_id != launched_request_id_) {
        // A warm-up does not preempt the worker (generation may have already given the result and be writing the
        // tail), it queues behind it.
        queued_warm_ = request;
        worker_cancel_ = warming;
      } else if (IsNewRequest(request, launched_request_id_, launched_generate_)) {
        StopWorker();
        CancelGeneration();
        queued_warm_.reset();
        Start(request);
      }
    }
  } else {
    StopWorker();
    if (queued_warm_.has_value()) {
      const auto queued = std::move(*queued_warm_);
      queued_warm_.reset();
      CancelGeneration();
      StopWorking();
      ProcessRequest(IsNewRequest(request, queued.request_id, false) ? request : queued);
    } else {
      ProcessRequest(request);
    }
  }
  // The tick takes what was computed before the launch: the answer to the tick's request comes no earlier than the
  // next tick, however fast the worker is.
  FlushEmitted();
  if (failed_request_.has_value()) {
    std::lock_guard lock(emitted_mutex_);
    emitted_.push_back(std::move(*failed_request_));
    failed_request_.reset();
  }
  LaunchWorker();
}

bool LlmGenerate::IsNewRequest(
  const AssistantRequest& request, const std::string& active_id, bool active_generate
) const {
  const bool failed_request = request.status == AssistantStatus::ERROR && !request.request_id.empty();
  return (request.kind != AssistantRequestKind::NONE || failed_request) && request.request_id != active_id
    && (request.kind == AssistantRequestKind::GENERATE || failed_request || !active_generate);
}

void LlmGenerate::ProcessRequest(const AssistantRequest& request) {
  if (request.status == AssistantStatus::ERROR) {
    if (request.request_id != last_request_id_) {
      last_request_id_ = request.request_id;
      FailRequest(request);
      return;
    }
    PushIdle();
    return;
  }

  if (request.kind == AssistantRequestKind::NONE || request.request_id == last_request_id_) {
    PushIdle();
    return;
  }

  Start(request);
}

void LlmGenerate::Start(const AssistantRequest& request) {
  last_request_id_ = request.request_id;
  if (request.status == AssistantStatus::ERROR) {
    FailRequest(request);
    StopWorking();
    return;
  }
  active_request_id_ = request.request_id;
  active_generate_ = request.kind == AssistantRequestKind::GENERATE;
  pin_after_prefill_ = request.kind == AssistantRequestKind::WARM && !request.messages.empty()
    && request.messages.back().role == MessageRole::SCENE;
  active_max_tokens_ = request.generation.max_tokens > 0 ? request.generation.max_tokens : params_.max_new_tokens;
  interpreter_->SetSamplingTemperature(request.generation.temperature.value_or(params_.llm_interpreter_params.temp));
  reused_tokens_ = 0;
  prefilled_tokens_ = 0;
  started_at_ = std::chrono::steady_clock::now();
  prefilled_at_ = {};
  generated_count_ = 0;
  generated_text_.clear();
  generated_ends_.clear();
  generated_tokens_.clear();
  filter_.Reset();
  // The head does not decide on steps after tool results.
  classify_prompt_ = interpreter_->PromptHeadTokens() > 0 && IsUserTurn(request);
  allow_decline_ = classify_prompt_ && !request.follow_up;
  prompt_label_.clear();
  prompt_probabilities_.clear();

  if (IsAsync()) {
    // Prompt tokenization takes seconds on a weak CPU: keep it out of the graph tick.
    planned_request_ = request;
    state_ = State::PLAN;
    PushIdle();
    KeepWorking();
    return;
  }
  Plan(request);
}

void LlmGenerate::Plan(const AssistantRequest& request) {
  if (!PlanPrefill(request)) {
    ClearActive();
    return;
  }
  if (pending_tokens_.empty()) {
    FinishPrefill();
    return;
  }
  state_ = State::PREFILL;
  PushIdle();
  KeepWorking();
}

bool LlmGenerate::PlanPrefill(const AssistantRequest& request) {
  const size_t capacity = Capacity();
  const size_t reserve = active_generate_ ? static_cast<size_t>(std::max(0, active_max_tokens_)) : 0;
  std::string error;

  auto plan = planner_.Plan(request, kv_, capacity, reserve, MinTailTokens(), error);
  // The window cache does not reach the plan point: roll back to the pinned point and plan from it, otherwise start
  // over.
  if (plan.has_value() && !interpreter_->TruncateContextTo(plan->keep_tokens) && RollbackToPin(plan->keep_tokens)) {
    plan = planner_.Plan(request, kv_, capacity, reserve, MinTailTokens(), error);
  }
  if (plan.has_value() && !interpreter_->TruncateContextTo(plan->keep_tokens)) {
    ResetContext("truncate");
    plan = planner_.Plan(request, kv_, capacity, reserve, MinTailTokens(), error);
  }
  if (!plan.has_value()) {
    FailActive(error);
    return false;
  }
  if (plan->keep_tokens < pinned_tokens_) {
    pinned_tokens_ = 0; // a rollback below the pinned point removes it
  }
  return AcceptPlan(*plan);
}

bool LlmGenerate::AcceptPlan(const PromptPlan& plan) {
  kv_.Truncate(plan.keep_tokens);
  DumpPrompt("prompt", plan.prompt);
  DumpPrompt("tail", plan.tail);
  pending_text_ = plan.tail;
  pending_tokens_ = plan.tail_tokens;
  pending_ends_.clear();
  const bool bos_expected = plan.keep_tokens == 0;
  if (
    !TokenByteEnds(pending_tokens_, bos_expected, pending_text_.size(), pending_ends_)
    && !TokenByteEnds(pending_tokens_, !bos_expected, pending_text_.size(), pending_ends_)
  ) {
    pending_ends_.clear();
    if (params_.local_debug_enabled) {
      std::cerr << "[llm_generate] token bytes do not add up to the prompt tail, kv reuse is coarse" << std::endl;
    }
  }
  pending_offset_ = 0;
  pending_chunks_ = 0;
  reused_tokens_ = plan.keep_tokens;
  debug_.StartPrefill(*interpreter_, plan.tail);
  debug_.LogPrefillJobStarted(
    active_generate_ ? "generate" : "warm",
    active_request_id_,
    plan.keep_tokens + pending_tokens_.size(),
    pending_tokens_.size(),
    static_cast<size_t>(std::max(1, params_.prefill_chunk_tokens))
  );
  return true;
}

bool LlmGenerate::TokenByteEnds(
  const std::vector<LlmToken>& tokens, bool skip_first, size_t text_size, std::vector<uint32_t>& ends
) {
  ends.clear();
  ends.reserve(tokens.size());
  size_t offset = 0;
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (i == 0 && skip_first) {
      ends.push_back(0);
      continue;
    }
    offset += interpreter_->Decode(tokens[i]).size();
    if (offset > text_size) {
      return false;
    }
    ends.push_back(static_cast<uint32_t>(offset));
  }
  return offset == text_size;
}

void LlmGenerate::ProcessLoopIterationImpl() {
  Step();
}

void LlmGenerate::Step() {
  if (state_ == State::PLAN) {
    Plan(planned_request_);
    return;
  }
  if (state_ == State::PREFILL) {
    PrefillStep();
    return;
  }
  if (state_ == State::GENERATING) {
    GenerateStep();
    return;
  }
  StopWorking();
}

void LlmGenerate::ProcessLoopInterruptImpl() {
  PushIdle();
  FinishLoop();
}

void LlmGenerate::PrefillStep() {
  const size_t chunk_cap = static_cast<size_t>(std::max(1, params_.prefill_chunk_tokens));
  const size_t end = std::min(pending_offset_ + chunk_cap, pending_tokens_.size());
  const bool last = end >= pending_tokens_.size();
  const std::vector<LlmToken> chunk(
    pending_tokens_.begin() + static_cast<std::ptrdiff_t>(pending_offset_),
    pending_tokens_.begin() + static_cast<std::ptrdiff_t>(end)
  );
  const auto started = std::chrono::steady_clock::now();
  try {
    if (!interpreter_->AppendPromptTokens(chunk, active_generate_ && last)) {
      debug_.LogPrefillFailed("Failed to append prompt tokens.");
      ResetContext("append");
      FailActive("Failed to append prompt tokens.");
      ClearActive();
      if (!StartDeferred()) {
        StopWorking();
      }
      return;
    }
  } catch (const std::exception& exception) {
    ResetContext("append");
    FailActive(exception.what());
    ClearActive();
    if (!StartDeferred()) {
      StopWorking();
    }
    return;
  }
  debug_.LogPrefillChunkFinished(
    active_generate_ ? "generate" : "warm",
    active_request_id_,
    pending_offset_,
    chunk.size(),
    ContextTokens(),
    MsBetween(started, std::chrono::steady_clock::now())
  );
  // The transcript follows the cache chunk by chunk: an interrupted prefill keeps what was computed. Without token
  // boundaries only as a whole; the excess in the cache after a cancel is cut off by the next plan.
  if (!pending_ends_.empty()) {
    const uint32_t from = pending_offset_ == 0 ? 0 : pending_ends_[pending_offset_ - 1];
    std::vector<uint32_t> ends(
      pending_ends_.begin() + static_cast<std::ptrdiff_t>(pending_offset_),
      pending_ends_.begin() + static_cast<std::ptrdiff_t>(end)
    );
    for (auto& byte_end : ends) {
      byte_end -= from;
    }
    kv_.Append(std::string_view(pending_text_).substr(from, ends.back()), ends, chunk);
  } else if (last) {
    kv_.Append(
      pending_text_,
      std::vector<uint32_t>(pending_tokens_.size(), static_cast<uint32_t>(pending_text_.size())),
      pending_tokens_
    );
  }
  pending_offset_ = end;
  prefilled_tokens_ = pending_offset_;
  ++pending_chunks_;
  if (!last) {
    PushIdle();
    KeepWorking();
    return;
  }
  debug_.LogPrefillFinished();
  debug_.LogPrefillJobFinished(
    active_generate_ ? "generate" : "warm", active_request_id_, pending_tokens_.size(), pending_chunks_, ContextTokens()
  );
  FinishPrefill();
}

void LlmGenerate::FinishPrefill() {
  prefilled_at_ = std::chrono::steady_clock::now();
  pending_text_.clear();
  pending_tokens_.clear();
  pending_ends_.clear();
  if (!active_generate_) {
    // A warm-up of the scene alone ends at its end: history trimming later rolls back here instead of recomputing
    // everything.
    if (pin_after_prefill_) {
      interpreter_->PinRollbackPoint();
      pinned_tokens_ = kv_.Tokens();
    }
    ClearActive();
    if (StartDeferred()) {
      return;
    }
    PushIdle();
    StopWorking();
    return;
  }
  if (classify_prompt_ && DeclinedByPromptHead()) {
    Decline();
    return;
  }
  state_ = State::GENERATING;
  debug_.MarkGenerationStarted();
  drafts_at_start_ = interpreter_->AcceptedDrafts();
  PushIdle();
  KeepWorking();
}

size_t LlmGenerate::MinTailTokens() const {
  if (classify_prompt_) {
    return interpreter_->PromptHeadTokens();
  }
  return active_generate_ ? 1 : 0;
}

bool LlmGenerate::DeclinedByPromptHead() {
  auto probabilities = interpreter_->PromptHeadProbabilities();
  if (!probabilities.has_value()) {
    // The plan always recomputes the tail of the head: we get here only on a failure, the LLM answers.
    if (params_.local_debug_enabled) {
      std::cerr << "[llm_generate] prompt head: tail states are missing" << std::endl;
    }
    return false;
  }
  const auto labels = interpreter_->PromptHeadLabels();
  const auto top = std::max_element(probabilities->begin(), probabilities->end()) - probabilities->begin();
  prompt_label_ = labels[static_cast<size_t>(top)];
  prompt_probabilities_ = std::move(*probabilities);
  const bool declined = allow_decline_ && Declines(params_.prompt_head, labels, prompt_probabilities_, prompt_label_);
  debug_.LogPromptHead(labels, prompt_probabilities_, prompt_label_, declined);
  return declined;
}

// A decline before generation: the prompt stays in the KV, the next plan cuts it by the shared prefix.
void LlmGenerate::Decline() {
  state_ = State::IDLE;
  Push(MakeFinalChunk(StreamFinishReason::DECLINED));
  ClearActive();
  StopWorking();
}

void LlmGenerate::GenerateStep() {
  const size_t capacity = Capacity();
  if (generated_count_ >= active_max_tokens_ || (capacity > 0 && ContextTokens() >= capacity)) {
    FinishGeneration(StreamFinishReason::MAX_TOKENS, AssistantStatus::OK);
    return;
  }
  LlmToken token = 0;
  try {
    token = interpreter_->PredictNextToken();
  } catch (const std::exception& exception) {
    FinishGeneration(StreamFinishReason::ERROR, AssistantStatus::ERROR, exception.what());
    return;
  }
  if (token == interpreter_->GetEosToken()) {
    FinishGeneration(StreamFinishReason::EOS, AssistantStatus::OK);
    return;
  }
  ++generated_count_;
  const std::string piece = interpreter_->Decode(token);
  generated_text_ += piece;
  generated_ends_.push_back(static_cast<uint32_t>(generated_text_.size()));
  generated_tokens_.push_back(token);

  auto out = filter_.Feed(piece);
  Push(MakeTextChunk(std::move(out.text), piece));
  if (!PushToolCalls(out)) {
    FinishGeneration(StreamFinishReason::ERROR, AssistantStatus::ERROR, kCallSourceMismatch);
  }
}

std::vector<TextSpan> LlmGenerate::GeneratedSpans(size_t begin, size_t end, size_t shift) const {
  std::vector<TextSpan> spans;
  size_t token_begin = 0;
  for (size_t i = 0; i < generated_tokens_.size() && token_begin < end; ++i) {
    const size_t token_end = generated_ends_[i];
    const size_t from = std::max(token_begin, begin);
    const size_t to = std::min<size_t>(token_end, end);
    if (from < to) {
      const auto origin = interpreter_->IsSpecialToken(generated_tokens_[i])
        ? TextOrigin::VERIFIED
        : TextOrigin::FOREIGN;
      spans.push_back({from - begin + shift, to - begin + shift, origin});
    }
    token_begin = token_end;
  }
  return spans;
}

void LlmGenerate::PushHistory() {
  auto history = EscapeSpecialTexts(
    generated_text_, GeneratedSpans(0, generated_text_.size(), 0), interpreter_->SpecialTokenTexts()
  );
  if (params_.local_debug_enabled && history != generated_text_) {
    // Each escape is one insertion of U+200B or one replacement with U+FFFD.
    const auto count = [](const std::string& text, std::string_view what) {
      size_t found = 0;
      for (auto at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) {
        ++found;
      }
      return found;
    };
    const size_t escapes = count(history, "\xE2\x80\x8B") - count(generated_text_, "\xE2\x80\x8B")
      + count(history, "\xEF\xBF\xBD") - count(generated_text_, "\xEF\xBF\xBD");
    std::cerr << "[llm_generate] history escaped " << escapes << std::endl;
  }
  auto chunk = MakeTextChunk("", "");
  std::get<TextDelta>(chunk.payload).history = std::move(history);
  Push(std::move(chunk));
}

bool LlmGenerate::PushToolCalls(StreamFilterOutput& out) {
  for (size_t i = 0; i < out.calls.size(); ++i) {
    auto& raw = out.calls[i].raw_text;
    const auto& source = out.sources[i];
    std::vector<TextSpan> spans;
    size_t raw_end = 0;
    for (const auto& piece : source.pieces) {
      const size_t from = source.offset + piece.call_begin;
      if (
        piece.raw_begin < raw_end || piece.raw_begin + piece.size > raw.size()
        || from + piece.size > generated_text_.size()
        || raw.compare(piece.raw_begin, piece.size, generated_text_, from, piece.size) != 0
      ) {
        return false;
      }
      const auto piece_spans = GeneratedSpans(from, from + piece.size, piece.raw_begin);
      spans.insert(spans.end(), piece_spans.begin(), piece_spans.end());
      raw_end = piece.raw_begin + piece.size;
    }
    raw = EscapeSpecialTexts(raw, spans, interpreter_->SpecialTokenTexts());
  }
  for (auto& call : out.calls) {
    Push(MakeToolCallChunk(std::move(call)));
  }
  return true;
}

std::string LlmGenerate::StopSequenceMissingSuffix() const {
  const auto& stop = params_.llm_interpreter_params.stop_sequence;
  if (stop.empty()) {
    return "";
  }
  const size_t present = SuffixPrefixOverlap(generated_text_, stop);
  return present >= stop.size() ? "" : stop.substr(present);
}

void LlmGenerate::RecordGenerated(const std::string& stop_tail, const std::vector<LlmToken>& stop_tokens) {
  std::vector<uint32_t> ends = generated_ends_;
  std::vector<LlmToken> tokens = generated_tokens_;
  std::string text = generated_text_;
  if (!stop_tail.empty()) {
    std::vector<uint32_t> stop_ends;
    if (!TokenByteEnds(stop_tokens, false, stop_tail.size(), stop_ends)) {
      stop_ends.assign(stop_tokens.size(), static_cast<uint32_t>(stop_tail.size()));
    }
    for (const auto end : stop_ends) {
      ends.push_back(static_cast<uint32_t>(text.size()) + end);
    }
    text += stop_tail;
    tokens.insert(tokens.end(), stop_tokens.begin(), stop_tokens.end());
  }
  kv_.Append(text, ends, tokens);
}

void LlmGenerate::FinishGeneration(StreamFinishReason reason, AssistantStatus status, const std::string& error) {
  debug_.LogGenerationFinished(
    reason, status, generated_count_, interpreter_->AcceptedDrafts() - drafts_at_start_, error
  );
  StreamFinishReason reported = reason;
  const bool natural_stop = reason == StreamFinishReason::EOS || reason == StreamFinishReason::STOP_SEQUENCE;

  std::string stop_tail;
  std::vector<LlmToken> stop_tokens;
  if (status == AssistantStatus::ERROR) {
    ResetContext("generation error");
  } else if (natural_stop) {
    stop_tail = StopSequenceMissingSuffix();
    if (!stop_tail.empty()) {
      const auto tokens = interpreter_->TokenizePromptPart(stop_tail, false);
      const size_t capacity = Capacity();
      if (tokens.has_value() && (capacity == 0 || ContextTokens() + tokens->size() <= capacity)) {
        stop_tokens = *tokens;
      } else {
        stop_tail.clear();
      }
    }
    if (reason == StreamFinishReason::EOS && !stop_tokens.empty()) {
      reported = StreamFinishReason::STOP_SEQUENCE;
    }
  }

  auto tail = filter_.Finish(natural_stop);
  if (!tail.text.empty()) {
    Push(MakeTextChunk(std::move(tail.text), ""));
  }
  const bool calls_match = PushToolCalls(tail);
  PushHistory();

  AssistantChunk final = status == AssistantStatus::ERROR ? MakeErrorChunk(error, reason)
    : !calls_match                                        ? MakeErrorChunk(kCallSourceMismatch, reason)
    : tail.incomplete_call                                ? MakeErrorChunk("Incomplete tool call", reason)
    : tail.malformed_call                                 ? MakeErrorChunk("Malformed tool call", reason)
                                                          : MakeFinalChunk(reported);
  state_ = State::IDLE;
  Push(std::move(final));

  // The stop sequence tail goes into the KV after the result is given: the call and the answer do not wait for it.
  if (status != AssistantStatus::ERROR) {
    bool appended = true;
    try {
      if (!stop_tokens.empty()) {
        appended = interpreter_->AppendPromptTokens(stop_tokens, false);
      }
    } catch (const std::exception&) {
      appended = false;
    }
    if (appended) {
      debug_.LogGenerationStopTail(reason, reported, stop_tokens.size(), generated_text_);
      RecordGenerated(stop_tail, stop_tokens);
      DumpPrompt("generated", generated_text_ + stop_tail);
    } else {
      ResetContext("stop tail append");
    }
  }
  ClearActive();
  StopWorking();
}

void LlmGenerate::CancelGeneration() {
  if (state_ == State::GENERATING) {
    interpreter_->StopGeneration();
    debug_.LogGenerationFinished(
      StreamFinishReason::CANCELLED,
      AssistantStatus::OK,
      generated_count_,
      interpreter_->AcceptedDrafts() - drafts_at_start_
    );
    RecordGenerated("", {});
    filter_.Finish();
    PushHistory();
    Push(MakeFinalChunk(StreamFinishReason::CANCELLED));
  } else if (state_ == State::PLAN || state_ == State::PREFILL) {
    Push(MakeFinalChunk(StreamFinishReason::CANCELLED));
  }
  ClearActive();
  // A request that waited for the end of the warm-up is cancelled together with it, with its own final.
  if (deferred_.has_value()) {
    AssistantChunk cancelled = MakeFinalChunk(StreamFinishReason::CANCELLED);
    cancelled.meta.request_id = deferred_->request_id;
    deferred_.reset();
    Push(std::move(cancelled));
  }
}

void LlmGenerate::ClearActive() {
  state_ = State::IDLE;
  active_request_id_.clear();
  active_generate_ = false;
  pending_text_.clear();
  pending_tokens_.clear();
  pending_ends_.clear();
  generated_count_ = 0;
  generated_text_.clear();
  generated_ends_.clear();
  generated_tokens_.clear();
  classify_prompt_ = false;
  allow_decline_ = false;
  prompt_label_.clear();
  prompt_probabilities_.clear();
  debug_.Reset();
}

bool LlmGenerate::StartDeferred() {
  if (!deferred_.has_value()) {
    return false;
  }
  const AssistantRequest request = std::move(*deferred_);
  deferred_.reset();
  Start(request);
  return true;
}

bool LlmGenerate::RollbackToPin(size_t keep_tokens) {
  if (pinned_tokens_ == 0 || pinned_tokens_ > keep_tokens || !interpreter_->TruncateContextTo(pinned_tokens_)) {
    return false;
  }
  kv_.Truncate(pinned_tokens_);
  return true;
}

bool LlmGenerate::ResetContext(const std::string& why) {
  pinned_tokens_ = 0;
  kv_.Clear();
  if (interpreter_->TruncateContextTo(0)) {
    return true;
  }
  interpreter_->ClearContext();
  if (params_.local_debug_enabled) {
    std::cerr << "[llm_generate] context reset (" << why << ")" << std::endl;
  }
  return true;
}

size_t LlmGenerate::Capacity() const {
  return interpreter_->GetContextCapacity().value_or(0);
}

size_t LlmGenerate::ContextTokens() const {
  return interpreter_->GetContextTokenCount().value_or(kv_.Tokens());
}

GenerationUsage LlmGenerate::Usage(bool with_timings) const {
  GenerationUsage usage{
    .context_tokens = ContextTokens(),
    .reused_tokens = reused_tokens_,
    .prefilled_tokens = prefilled_tokens_,
    .generated_tokens = static_cast<size_t>(generated_count_),
    .prompt_label = prompt_label_,
  };
  if (with_timings) {
    const auto now = std::chrono::steady_clock::now();
    const auto prefilled = prefilled_at_ == std::chrono::steady_clock::time_point{} ? now : prefilled_at_;
    usage.prefill_ms = MsBetween(started_at_, prefilled);
    usage.generation_ms = MsBetween(prefilled, now);
  }
  return usage;
}

LlmGenerate::Emitted LlmGenerate::MakeEmitted(AssistantChunk chunk) const {
  const RunnerPhase phase = state_ == State::GENERATING ? RunnerPhase::GENERATING
    : state_ != State::IDLE                             ? RunnerPhase::PREFILLING
                                                        : RunnerPhase::IDLE;
  chunk.meta.phase = phase;
  if (chunk.meta.request_id.empty()) {
    chunk.meta.request_id = active_request_id_;
  }
  // A request error before the start arrives with a foreign id.
  const bool own_final = chunk.meta.is_final && !active_request_id_.empty()
    && chunk.meta.request_id == active_request_id_;
  const bool foreign_final = chunk.meta.is_final && !own_final;
  if (own_final) {
    chunk.meta.usage = Usage(true);
  }
  std::optional<LlmReport> report;
  if (with_report_) {
    report = LlmReport{
      .request_id = chunk.meta.request_id,
      .phase = phase,
      .usage = own_final ? chunk.meta.usage
        : foreign_final  ? GenerationUsage{}
                         : Usage(false),
      .is_final = chunk.meta.is_final,
      .finish_reason = chunk.meta.finish_reason,
      .status = chunk.meta.status,
      .error = chunk.meta.error,
      .prompt_probabilities = foreign_final ? std::vector<float>{} : prompt_probabilities_,
    };
  }
  return {std::move(chunk), std::move(report)};
}

void LlmGenerate::Emit(Emitted emitted) {
  if (emitted.report.has_value()) {
    PushLlmReport(std::move(*emitted.report));
  }
  PushStreamChunk(std::move(emitted.chunk));
}

void LlmGenerate::Push(AssistantChunk chunk) {
  auto emitted = MakeEmitted(std::move(chunk));
  if (!IsAsync()) {
    Emit(std::move(emitted));
    return;
  }
  std::lock_guard lock(emitted_mutex_);
  emitted_.push_back(std::move(emitted));
}

void LlmGenerate::PushIdle() {
  Push(MakeIdleChunk());
}

void LlmGenerate::FailActive(const std::string& error) {
  if (active_generate_) {
    Push(Failure(active_request_id_, error));
    return;
  }
  if (params_.local_debug_enabled) {
    std::cerr << "[llm_generate] warm '" << active_request_id_ << "' failed: " << error << '\n';
  }
  PushIdle();
}

void LlmGenerate::FailRequest(const AssistantRequest& request) {
  auto chunk = Failure(request.request_id, request.error.empty() ? "upstream request error" : request.error);
  if (IsAsync()) {
    failed_request_ = MakeEmitted(std::move(chunk));
  } else {
    Push(std::move(chunk));
  }
}

AssistantChunk LlmGenerate::Failure(const std::string& request_id, const std::string& error) const {
  if (params_.local_debug_enabled) {
    std::cerr << "[llm_generate] request '" << request_id << "' failed: " << error << std::endl;
  }
  AssistantChunk chunk = MakeErrorChunk(error);
  chunk.meta.request_id = request_id;
  return chunk;
}

void LlmGenerate::KeepWorking() {
  if (IsAsync()) {
    working_ = true;
  } else {
    StartLoop();
  }
}

void LlmGenerate::StopWorking() {
  if (IsAsync()) {
    working_ = false;
  } else {
    FinishLoop();
  }
}

void LlmGenerate::LaunchWorker() {
  // While busy, the worker exclusively owns working_ and state_. Checking it first makes the acquire in
  // IsWorkerBusy() the hand-off before reading that state. Do not relaunch a worker that stopped in this tick for
  // the sake of a queued warm-up: the next tick takes the warm-up.
  if (!IsAsync() || IsWorkerBusy() || queued_warm_.has_value() || !working_ || state_ == State::IDLE) {
    return;
  }
  launched_request_id_ = active_request_id_;
  launched_generate_ = active_generate_;
  {
    std::lock_guard lock(emitted_mutex_);
    idle_ = MakeEmitted(MakeIdleChunk());
  }
  worker_cancel_ = false;
  worker_busy_.store(true, std::memory_order_release);
  worker_task_ = std::make_shared<Task>(
    [this] {
      RunWorker();
    },
    params_.async_thread_priority
  );
  worker_pool_->Push(worker_task_);
}

void LlmGenerate::RunWorker() {
  while (working_ && !worker_cancel_) {
    Step();
    auto idle = MakeEmitted(MakeIdleChunk());
    std::lock_guard lock(emitted_mutex_);
    idle_ = std::move(idle);
  }
  worker_busy_.store(false, std::memory_order_release);
}

void LlmGenerate::StopWorker() {
  if (!worker_task_) {
    return;
  }
  worker_cancel_ = true;
  if (worker_pool_->Cancel(worker_task_)) {
    worker_busy_.store(false, std::memory_order_release);
  } else {
    worker_pool_->Wait(worker_task_);
  }
  worker_task_.reset();
}

bool LlmGenerate::IsWorkerBusy() const {
  return worker_busy_.load(std::memory_order_acquire);
}

void LlmGenerate::FlushEmitted() {
  std::vector<Emitted> emitted;
  {
    std::lock_guard lock(emitted_mutex_);
    emitted.swap(emitted_);
    if (emitted.empty() && IsWorkerBusy()) {
      emitted.push_back(idle_);
    }
  }
  if (emitted.empty()) {
    emitted.push_back(MakeEmitted(MakeIdleChunk()));
  }
  for (auto& item : emitted) {
    Emit(std::move(item));
  }
}

void LlmGenerate::DumpPrompt(const std::string& kind, const std::string& text) const {
  if (!params_.debug_dump_prompt) {
    return;
  }
  std::cerr << "[prompt_dump] kind=" << kind << " request_id=\"" << active_request_id_ << "\" bytes=" << text.size()
            << " text=" << JsonQuote(text) << std::endl;
}

} // namespace giga_edge
