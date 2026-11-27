#include "giga_edge/modules/llm/nodes/llm_generate/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

LlmInterpreterParams ParseLlmInterpreterParams(const YAML::Node& config) {
  LlmInterpreterParams result;
  if (!config["model_path"]) {
    throw std::runtime_error("LLM interpreter params requires 'model_path'.");
  }
  result.model_path = config["model_path"].as<std::string>();
  result.stop_sequence = config["stop_sequence"].as<std::string>("");
  result.eog_tokens = config["eog_tokens"].as<std::vector<std::string>>(result.eog_tokens);
  result.seed = config["seed"].as<int32_t>(result.seed);
  result.top_k = config["top_k"].as<int32_t>(result.top_k);
  result.top_p = config["top_p"].as<float>(result.top_p);
  result.temp = config["temp"].as<float>(result.temp);
  result.n_ctx = config["n_ctx"].as<int32_t>(result.n_ctx);
  result.n_batch = config["n_batch"].as<int32_t>(result.n_batch);
  result.n_threads = ParseThreadCount(config["n_threads"], result.n_threads);
  result.n_threads_batch = ParseThreadCount(config["n_threads_batch"], result.n_threads_batch);
  result.flash_attn = config["flash_attn"].as<std::string>(result.flash_attn);
  result.kv_cache_type = config["kv_cache_type"].as<std::string>(result.kv_cache_type);
  result.use_mmap = config["use_mmap"].as<bool>(result.use_mmap);
  result.use_mlock = config["use_mlock"].as<bool>(result.use_mlock);
  result.lookup_draft_tokens = config["lookup_draft_tokens"].as<int32_t>(result.lookup_draft_tokens);
  result.swa_full = config["swa_full"].as<bool>(result.swa_full);
  result.mtp_draft_max = config["mtp_draft_max"].as<int32_t>(result.mtp_draft_max);
  return result;
}

template<>
LlmGenerateParams ParseConfig(const YAML::Node& config) {
  LlmGenerateParams result;
  if (config["template"]) {
    result.prompt_template = ParsePromptTemplateParams(config["template"]);
  }
  if (const auto parser = config["tool_call_parser"]; parser) {
    if (parser.IsScalar()) {
      result.tool_call_parser = parser.as<std::string>();
    } else {
      result.tool_call_parser = parser["name"].as<std::string>();
      result.tool_call_parser_params = parser;
    }
  }
  const auto policy = config["on_new_request"].as<std::string>("ignore");
  if (policy == "replace") {
    result.on_new_request = OnNewRequestPolicy::REPLACE;
  } else if (policy != "ignore") {
    throw std::runtime_error("llm_generate: unknown on_new_request policy: " + policy);
  }
  result.max_new_tokens = config["max_new_tokens"].as<int>(result.max_new_tokens);
  result.prefill_chunk_tokens = config["prefill_chunk_tokens"].as<int>(result.prefill_chunk_tokens);
  result.local_debug_enabled = config["local_debug_enabled"].as<bool>(result.local_debug_enabled);
  result.debug_dump_prompt = config["debug_dump_prompt"].as<bool>(result.debug_dump_prompt);
  if (!config["llm_interpreter_params"]) {
    throw std::runtime_error("llm_generate requires 'llm_interpreter_params' section.");
  }
  result.llm_interpreter_params = ParseLlmInterpreterParams(config["llm_interpreter_params"]);
  if (const auto head = config["prompt_head"]; head) {
    result.prompt_head.path = head["path"].as<std::string>();
    const auto decision = head["decision"] ? head["decision"]["type"].as<std::string>("argmax") : "argmax";
    if (decision == "threshold") {
      result.prompt_head.decision = PromptHeadDecision::THRESHOLD;
      result.prompt_head.threshold = head["decision"]["threshold"].as<float>();
      if (!(result.prompt_head.threshold >= 0.0f && result.prompt_head.threshold <= 1.0f)) {
        throw std::runtime_error("llm_generate: prompt_head threshold must be in [0, 1]");
      }
    } else if (decision != "argmax") {
      throw std::runtime_error("llm_generate: unknown prompt_head decision: " + decision);
    }
    result.prompt_head.decline_labels = head["decline_labels"].as<std::vector<std::string>>(std::vector<std::string>{});
    result.llm_interpreter_params.prompt_head_path = result.prompt_head.path;
  }
  if (const auto strategy = config["inference_strategy"]; strategy) {
    const auto type = strategy["type"].as<std::string>("sync");
    if (type == "wait_till_inferring") {
      result.async_thread_pool = strategy["thread_pool_name"].as<std::string>("");
      if (result.async_thread_pool.empty()) {
        throw std::runtime_error("llm_generate: 'thread_pool_name' for async strategy is not specified");
      }
      result.async_thread_priority = strategy["thread_priority"].as<int>(0);
    } else if (type != "sync") {
      throw std::runtime_error("llm_generate: unsupported inference_strategy: " + type);
    }
  }
  return result;
}

} // namespace giga_edge
