#pragma once

#include "params.h"

#include "giga_edge/lib/data_reader/data_reader.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace giga_edge {

using LlmToken = int32_t;

// The common interface of LLM interpreters.
class ILlmInterpreter {
public:
  virtual ~ILlmInterpreter() = default;

  virtual bool Initialize(const LlmInterpreterParams& params, const IDataReaderPtr& data_reader) = 0;

  virtual void ClearContext() = 0;

  // Tokenizes a fully-rendered prompt as if it starts at an empty LLM context.
  virtual std::optional<std::vector<LlmToken>> TokenizePrompt(const std::string& prompt) const {
    return std::nullopt;
  }

  // Tokenizes prompt text with explicit BOS handling. Use prepend_bos=false for text appended to an existing context.
  virtual std::optional<std::vector<LlmToken>> TokenizePromptPart(const std::string& prompt, bool prepend_bos) const {
    return std::nullopt;
  }

  // A special token at which the tokenizer splits the text: the text after it is tokenized independently of the
  // text before it.
  virtual bool IsTokenizationBoundary(LlmToken /*token*/) const {
    return false;
  }

  // The texts of the tokens that prompt tokenization parses as special tokens (CONTROL and UNKNOWN), and the test
  // for such a token.
  virtual const std::vector<std::string>& SpecialTokenTexts() const {
    static const std::vector<std::string> none;
    return none;
  }
  virtual bool IsSpecialToken(LlmToken /*token*/) const {
    return false;
  }

  // Adds pre-tokenized prompt tokens at the current context position.
  virtual bool AppendPromptTokens(const std::vector<LlmToken>& tokens, bool calculate_logits = true) {
    return false;
  }

  // Removes all tokens after token_count from the active sequence.
  virtual bool TruncateContextTo(size_t token_count) {
    return false;
  }

  virtual void PinRollbackPoint() {}

  virtual size_t AcceptedDrafts() const {
    return 0;
  }
  // The head over the prompt: its labels and how many last prompt tokens it reads (the prefill recomputes them).
  virtual std::vector<std::string> PromptHeadLabels() const {
    return {};
  }
  virtual size_t PromptHeadTokens() const {
    return 0;
  }
  // nullopt: there is no head or the prompt tail is not fully computed.
  virtual std::optional<std::vector<float>> PromptHeadProbabilities() const {
    return std::nullopt;
  }

  virtual std::optional<size_t> GetContextTokenCount() const {
    return std::nullopt;
  }

  virtual std::optional<size_t> GetContextCapacity() const {
    return std::nullopt;
  }

  virtual std::optional<size_t> GetPromptTokenCount(const std::string& prompt) const {
    return std::nullopt;
  }

  virtual bool SetSamplingTemperature(float /*temperature*/) {
    return false;
  }

  virtual LlmToken PredictNextToken() = 0;

  virtual std::string Decode(LlmToken token) = 0;

  virtual void StopGeneration() = 0;

  virtual LlmToken GetEosToken() const = 0;

  virtual bool IsReady() const = 0;
};

class LlmInterpreterFactory {
public:
  enum class BackendType {
    LLAMA_CPP,
  };

  static std::unique_ptr<ILlmInterpreter> CreateInterpreter(BackendType type);
};

} // namespace giga_edge
