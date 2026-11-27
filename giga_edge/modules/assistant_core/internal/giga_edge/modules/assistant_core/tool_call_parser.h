#pragma once

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace giga_edge {

// Bytes of a call's raw_text taken from call_text verbatim; the other raw_text bytes come from the parser config.
struct RawTextSource {
  size_t raw_begin = 0;
  size_t call_begin = 0;
  size_t size = 0;
};

class IToolCallParser {
public:
  virtual ~IToolCallParser() = default;

  virtual std::optional<size_t> FindStart(std::string_view text) const = 0;
  virtual std::optional<size_t> FindEnd(std::string_view call_text, bool is_final) const = 0;
  virtual size_t PartialStartLength(std::string_view text) const = 0;
  virtual std::vector<ToolCall> Parse(std::string_view call_text) const = 0;
  virtual std::string CleanText(std::string_view text) const = 0;

  // Sources of raw_text for each call of Parse(call_text); by default raw_text is call_text verbatim.
  virtual std::vector<std::vector<RawTextSource>> RawTextSources(
    std::string_view call_text, const std::vector<ToolCall>& calls
  ) const {
    return std::vector<std::vector<RawTextSource>>(calls.size(), {RawTextSource{0, 0, call_text.size()}});
  }
};

using ToolCallParserPtr = std::shared_ptr<IToolCallParser>;
using ToolCallParserFactory = std::function<ToolCallParserPtr(const YAML::Node& params)>;

class ToolCallParserRegistry {
public:
  static bool Register(const std::string& name, ToolCallParserFactory factory);
  static ToolCallParserPtr Create(const std::string& name, const YAML::Node& params);
  static bool Has(const std::string& name);
  static std::vector<std::string> Names();
};

#define REGISTER_TOOL_CALL_PARSER(parser_name, factory)                                                                \
  inline const bool parser_name##_tool_call_parser_registered = ToolCallParserRegistry::Register(#parser_name, factory);

size_t PartialMarkerSuffix(std::string_view text, std::string_view marker);

// Origin of a call's raw_text: the start of call_text in the filter stream and the pieces taken from it verbatim.
struct ToolCallSource {
  size_t offset = 0;
  std::vector<RawTextSource> pieces;
};

struct StreamFilterOutput {
  std::string text;
  std::string raw;
  std::vector<ToolCall> calls;
  // One per call in calls.
  std::vector<ToolCallSource> sources;
  bool incomplete_call = false;
  bool malformed_call = false;
};

class StreamToolCallFilter {
public:
  explicit StreamToolCallFilter(ToolCallParserPtr parser = nullptr);

  StreamFilterOutput Feed(std::string_view delta);
  StreamFilterOutput Finish(bool allow_unterminated_call = true);
  void Reset();

private:
  StreamFilterOutput Drain(bool is_final);
  void TakeCall(std::string_view span, StreamFilterOutput& out);

  ToolCallParserPtr parser_;
  std::string buffer_;
  // How many bytes were fed to the filter since the last Reset.
  size_t fed_ = 0;
  bool in_call_ = false;
  bool malformed_ = false;
};

} // namespace giga_edge
