#pragma once

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/modules/assistant_core/prompt_template.h"
#include "giga_edge/modules/llm/interpreter/interpreter.h"

#include <string>
#include <vector>

namespace giga_edge {

enum class OnNewRequestPolicy {
  IGNORE,
  REPLACE,
};

enum class PromptHeadDecision {
  ARGMAX,
  THRESHOLD,
};

// The head over the prompt decides by the user message before generation. A decline (DECLINED): with argmax the
// most probable label is one of decline_labels; with threshold the total probability of the other labels is below
// threshold.
struct PromptHeadParams {
  std::string path;
  PromptHeadDecision decision = PromptHeadDecision::ARGMAX;
  float threshold = 0.5f;
  std::vector<std::string> decline_labels;
};

struct LlmGenerateParams {
  PromptTemplateParams prompt_template;
  std::string tool_call_parser;
  YAML::Node tool_call_parser_params;
  OnNewRequestPolicy on_new_request = OnNewRequestPolicy::IGNORE;
  int max_new_tokens = 128;
  int prefill_chunk_tokens = 128;
  bool local_debug_enabled = false;
  bool debug_dump_prompt = false;
  LlmInterpreterFactory::BackendType backend_type = LlmInterpreterFactory::BackendType::LLAMA_CPP;
  LlmInterpreterParams llm_interpreter_params;
  PromptHeadParams prompt_head;
  // Empty: generation runs inside the tick; otherwise in a pool of the environment, and the tick returns ready
  // chunks.
  std::string async_thread_pool;
  int async_thread_priority = 0;
};

LlmInterpreterParams ParseLlmInterpreterParams(const YAML::Node& config);

} // namespace giga_edge
