#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/lib/threadpool/threadpool.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"
#include "giga_edge/modules/assistant_core/prompt_template.h"
#include "giga_edge/modules/assistant_core/tool_call_parser.h"
#include "giga_edge/modules/llm/detail/llm_local_debug.h"
#include "giga_edge/modules/llm/interpreter/interpreter.h"
#include "giga_edge/modules/llm/kv_transcript.h"
#include "giga_edge/modules/llm/llm_report.h"
#include "giga_edge/modules/llm/nodes/llm_generate/params.h"
#include "giga_edge/modules/llm/prompt_planner.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace giga_edge {

class LlmGenerate : public NodeBase {
public:
  LlmGenerate(const NodeBaseConfig& base_config, const IDataReaderPtr& data_reader, const LlmGenerateParams& params);
  ~LlmGenerate() override;

  void ProcessOnce() override;
  void ProcessLoopIterationImpl() override;
  void ProcessLoopInterruptImpl() override;
  void Reset() override;

private:
  friend struct LlmGenerateTestAccess;

  REGISTER_OPTIONAL_INPUT(
    llm_generate, dialogue_reset, DialogueReset, bool, "Clears the active generation and KV state."
  );
  REGISTER_OPTIONAL_INPUT(
    llm_generate,
    cancel,
    Cancel,
    bool,
    "Drops the active generation (final CANCELLED); a running warm (async) and idle — nothing."
  );
  enum class State { IDLE, PLAN, PREFILL, GENERATING };

  struct Emitted {
    AssistantChunk chunk;
    std::optional<LlmReport> report;
  };

  bool IsAsync() const {
    return worker_pool_ != nullptr;
  }
  bool IsNewRequest(const AssistantRequest& request, const std::string& active_id, bool active_generate) const;
  void ProcessRequest(const AssistantRequest& request);
  void ProcessOnceAsync();
  void Step();
  void KeepWorking();
  void StopWorking();
  void LaunchWorker();
  void RunWorker();
  void StopWorker();
  bool IsWorkerBusy() const;
  void FlushEmitted();

  void Start(const AssistantRequest& request);
  void Plan(const AssistantRequest& request);
  bool PlanPrefill(const AssistantRequest& request);
  bool AcceptPlan(const PromptPlan& plan);
  void PrefillStep();
  void GenerateStep();
  void FinishPrefill();
  size_t MinTailTokens() const;
  bool DeclinedByPromptHead();
  void Decline();
  void FinishGeneration(StreamFinishReason reason, AssistantStatus status, const std::string& error = "");
  void CancelGeneration();
  // The answer bytes [begin, end) by tokens: a special token is a verified piece, an ordinary token is foreign text;
  // shifted by shift.
  std::vector<TextSpan> GeneratedSpans(size_t begin, size_t end, size_t shift) const;
  // The answer for the history: special texts made of ordinary tokens are escaped.
  void PushHistory();
  // raw_text of the calls is escaped by its sources in the answer; false: a parser source does not match raw_text.
  bool PushToolCalls(StreamFilterOutput& out);
  void ClearActive();
  bool StartDeferred();
  void RecordGenerated(const std::string& stop_tail, const std::vector<LlmToken>& stop_tokens);
  bool TokenByteEnds(
    const std::vector<LlmToken>& tokens, bool skip_first, size_t text_size, std::vector<uint32_t>& ends
  );
  std::string StopSequenceMissingSuffix() const;
  size_t Capacity() const;
  size_t ContextTokens() const;
  bool RollbackToPin(size_t keep_tokens);
  bool ResetContext(const std::string& why);
  GenerationUsage Usage(bool with_timings) const;
  Emitted MakeEmitted(AssistantChunk chunk) const;
  void Emit(Emitted emitted);
  void Push(AssistantChunk chunk);
  void PushIdle();
  void FailActive(const std::string& error);
  // The request arrived with an error: in async the result goes out on the next tick, like a worker answer.
  void FailRequest(const AssistantRequest& request);
  AssistantChunk Failure(const std::string& request_id, const std::string& error) const;
  void DumpPrompt(const std::string& kind, const std::string& text) const;

  REGISTER_INPUT(
    llm_generate,
    assistant_request,
    AssistantRequest,
    AssistantRequest,
    "Dialogue request: messages, tools and generation params. kind=WARM prefills only, kind=NONE is an idle tick."
  );
  REGISTER_BATCH_OUTPUT(
    llm_generate, stream_chunk, StreamChunk, AssistantChunk, "Generated text deltas and extracted tool calls."
  );
  REGISTER_OPTIONAL_BATCH_OUTPUT(
    llm_generate,
    llm_report,
    LlmReport,
    LlmReport,
    "Context and generation statistics; prefill and generation times on the final of a request."
  );

  LlmGenerateParams params_;
  std::unique_ptr<ILlmInterpreter> interpreter_;
  PromptTemplate template_;
  PromptPlanner planner_;
  ToolCallParserPtr parser_;
  StreamToolCallFilter filter_;
  KvTranscript kv_;
  LlmLocalDebugSession debug_;

  State state_ = State::IDLE;
  std::string active_request_id_;
  std::string last_request_id_;
  bool active_generate_ = false;
  // A warm-up of the scene alone: its end is the pinned rollback point for history trimming (0: no point).
  bool pin_after_prefill_ = false;
  size_t pinned_tokens_ = 0;
  int active_max_tokens_ = 0;
  std::optional<AssistantRequest> deferred_;
  AssistantRequest planned_request_;

  std::string pending_text_;
  std::vector<LlmToken> pending_tokens_;
  std::vector<uint32_t> pending_ends_;
  size_t pending_offset_ = 0;
  size_t pending_chunks_ = 0;

  size_t reused_tokens_ = 0;
  size_t prefilled_tokens_ = 0;
  std::chrono::steady_clock::time_point started_at_;
  std::chrono::steady_clock::time_point prefilled_at_;
  int generated_count_ = 0;
  size_t drafts_at_start_ = 0; // accepted drafts counter at the start of generation
  bool classify_prompt_ = false;
  bool allow_decline_ = false;
  std::string prompt_label_;
  std::vector<float> prompt_probabilities_;
  std::string generated_text_;
  std::vector<uint32_t> generated_ends_;
  std::vector<LlmToken> generated_tokens_;

  // async: while the worker is busy it owns the state above, and the tick sees only what is below.
  bool with_report_ = false;
  std::shared_ptr<IThreadPool> worker_pool_;
  TaskPtr worker_task_;
  std::atomic<bool> worker_busy_ = false;
  std::atomic<bool> worker_cancel_ = false;
  bool working_ = false;
  std::string launched_request_id_;
  bool launched_generate_ = false;
  // A warm-up that arrived while the worker was busy: it starts when the worker gets free.
  std::optional<AssistantRequest> queued_warm_;
  std::mutex emitted_mutex_;
  std::vector<Emitted> emitted_;
  Emitted idle_;
  // The result of a request that arrived with an error: held until the end of its tick, empty outside
  // ProcessOnceAsync.
  std::optional<Emitted> failed_request_;
};

REGISTER_NODE(
  llm_generate,
  LlmGenerate,
  LlmGenerateParams,
  true,
  "Runs a local LLM on an AssistantRequest: renders the prompt template, reuses the KV cache by bytes, streams "
  "chunks and extracts tool calls with the configured parser."
);

} // namespace giga_edge
