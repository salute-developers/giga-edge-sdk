#include "giga_edge/modules/assistant_core/tool_call_parser.h"

#include "giga_edge/modules/assistant_core/text.h"

#include <map>
#include <stdexcept>

namespace giga_edge {
namespace {

std::map<std::string, ToolCallParserFactory>& Factories() {
  static std::map<std::string, ToolCallParserFactory> factories;
  return factories;
}

} // namespace

bool ToolCallParserRegistry::Register(const std::string& name, ToolCallParserFactory factory) {
  if (!Factories().emplace(name, std::move(factory)).second) {
    throw std::runtime_error("Tool call parser is already registered: " + name);
  }
  return true;
}

ToolCallParserPtr ToolCallParserRegistry::Create(const std::string& name, const YAML::Node& params) {
  const auto it = Factories().find(name);
  if (it == Factories().end()) {
    throw std::runtime_error("Unknown tool call parser: " + name);
  }
  return it->second(params);
}

bool ToolCallParserRegistry::Has(const std::string& name) {
  return Factories().count(name) > 0;
}

std::vector<std::string> ToolCallParserRegistry::Names() {
  std::vector<std::string> names;
  for (const auto& [name, _] : Factories()) {
    names.push_back(name);
  }
  return names;
}

size_t PartialMarkerSuffix(std::string_view text, std::string_view marker) {
  return marker.empty() ? 0 : SuffixPrefixOverlap(text, marker.substr(0, marker.size() - 1));
}

StreamToolCallFilter::StreamToolCallFilter(ToolCallParserPtr parser)
  : parser_(std::move(parser)) {}

void StreamToolCallFilter::Reset() {
  buffer_.clear();
  fed_ = 0;
  in_call_ = false;
  malformed_ = false;
}

StreamFilterOutput StreamToolCallFilter::Feed(std::string_view delta) {
  buffer_.append(delta.data(), delta.size());
  fed_ += delta.size();
  auto out = Drain(false);
  out.raw.assign(delta.data(), delta.size());
  return out;
}

StreamFilterOutput StreamToolCallFilter::Finish(bool allow_unterminated_call) {
  auto out = Drain(allow_unterminated_call);
  out.incomplete_call = in_call_ || (parser_ && !buffer_.empty());
  out.malformed_call = malformed_;
  Reset();
  return out;
}

StreamFilterOutput StreamToolCallFilter::Drain(bool is_final) {
  StreamFilterOutput out;
  if (!parser_) {
    out.text = buffer_;
    buffer_.clear();
    return out;
  }
  while (!buffer_.empty()) {
    if (in_call_) {
      const auto end = parser_->FindEnd(buffer_, is_final);
      if (!end.has_value()) {
        break;
      }
      TakeCall(std::string_view(buffer_).substr(0, *end), out);
      buffer_.erase(0, *end);
      in_call_ = false;
      continue;
    }
    const auto start = parser_->FindStart(buffer_);
    if (start.has_value()) {
      out.text += parser_->CleanText(std::string_view(buffer_).substr(0, *start));
      buffer_.erase(0, *start);
      in_call_ = true;
      continue;
    }
    const size_t hold = is_final ? 0 : parser_->PartialStartLength(buffer_);
    const size_t release = buffer_.size() - hold;
    out.text += parser_->CleanText(std::string_view(buffer_).substr(0, release));
    buffer_.erase(0, release);
    break;
  }
  if (is_final && in_call_ && !buffer_.empty()) {
    TakeCall(buffer_, out);
    buffer_.clear();
    in_call_ = false;
  }
  return out;
}

void StreamToolCallFilter::TakeCall(std::string_view span, StreamFilterOutput& out) {
  auto calls = parser_->Parse(span);
  if (calls.empty()) {
    malformed_ = true;
    return;
  }
  auto pieces = parser_->RawTextSources(span, calls);
  if (pieces.size() != calls.size()) {
    malformed_ = true;
    return;
  }
  // span is the beginning of the buffer.
  const size_t offset = fed_ - buffer_.size();
  for (size_t i = 0; i < calls.size(); ++i) {
    if (calls[i].raw_text.empty()) {
      calls[i].raw_text = std::string(span);
    }
    out.calls.push_back(std::move(calls[i]));
    out.sources.push_back({offset, std::move(pieces[i])});
  }
}

} // namespace giga_edge
