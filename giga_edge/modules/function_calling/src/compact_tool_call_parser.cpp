#include "giga_edge/modules/function_calling/compact_tool_call_parser.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/modules/assistant_core/json.h"
#include "giga_edge/modules/assistant_core/text.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <stdexcept>

namespace giga_edge {
namespace {

const std::regex& TagPattern() {
  static const std::regex tag(R"(<[^>]+>)");
  return tag;
}

// With keep_tags a tag contains no "<": a bare "<" in the text does not swallow the mask after it.
const std::regex& KeepTagPattern() {
  static const std::regex tag(R"(<[^<>]+>)");
  return tag;
}

void SkipSpaces(std::string_view text, size_t& pos) {
  while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
    ++pos;
  }
}

bool IsNameChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::string JsonScalar(const std::string& value) {
  if (value == "true" || value == "false" || IsDecimalNumber(value)) {
    return value;
  }
  return JsonQuote(value);
}

// A value outside a tag: the model sometimes wraps it in <...> (<+10>, <true>).
std::string_view PlainValue(std::string_view raw) {
  auto value = TrimView(raw);
  if (!value.empty() && value.front() == '<') {
    value.remove_prefix(1);
  }
  if (!value.empty() && value.back() == '>') {
    value.remove_suffix(1);
  }
  return value;
}

struct Tag {
  std::string value;
  // Spelled with the first prefix of the format: <p_ro_x> -> <p_rw_x>.
  std::string canonical;
};

// The model may leave a tag unclosed (<p_rw_brightness<arg_sep>): the name then ends at the next '<'.
std::optional<Tag> TakeTag(std::string_view text, size_t& pos, const CompactArgFormat& format) {
  for (const auto& prefix : format.tag_prefixes) {
    const std::string open = "<" + prefix;
    if (text.compare(pos, open.size(), open) != 0) {
      continue;
    }
    size_t end = pos + open.size();
    while (end < text.size() && IsNameChar(text[end])) {
      ++end;
    }
    if (end == pos + open.size()) {
      continue;
    }
    std::string name(text.substr(pos + open.size(), end - pos - open.size()));
    Tag tag{name, "<" + format.tag_prefixes.front() + name + ">"};
    if (end < text.size() && text[end] == '>') {
      ++end;
    }
    if (format.indexed) {
      const size_t digits = end;
      while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) {
        ++end;
      }
      if (end == digits) {
        continue;
      }
      const std::string index(text.substr(digits, end - digits));
      tag.value += "_" + index;
      tag.canonical += index;
    }
    pos = end;
    return tag;
  }
  return std::nullopt;
}

// A tag is any opening of a format prefix, even without a name and an index: an unfinished tag does not end up in
// the value of its neighbor.
bool HasTag(std::string_view text, const CompactArgFormat& format) {
  return std::any_of(format.tag_prefixes.begin(), format.tag_prefixes.end(), [&text](const std::string& prefix) {
    return text.find("<" + prefix) != std::string_view::npos;
  });
}

size_t UnclosedTagSuffix(std::string_view text) {
  const auto lt = text.rfind('<');
  if (lt == std::string_view::npos || text.find('>', lt) != std::string_view::npos) {
    return 0;
  }
  return text.size() - lt;
}

CompactArgFormat ParseArgFormat(const YAML::Node& node) {
  CompactArgFormat format;
  format.tag_prefixes = node["tag_prefixes"].as<std::vector<std::string>>(format.tag_prefixes);
  format.indexed = node["indexed"].as<bool>(format.indexed);
  const auto type = node["type"].as<std::string>("auto");
  if (type != "auto" && type != "string") {
    throw std::invalid_argument("Compact argument type must be auto or string: " + type);
  }
  format.as_string = type == "string";
  if (format.indexed && format.tag_prefixes.empty()) {
    throw std::invalid_argument("Indexed compact argument needs tag_prefixes");
  }
  return format;
}

} // namespace

CompactToolCallParserParams ParseCompactToolCallParserParams(const YAML::Node& config) {
  CompactToolCallParserParams result;
  if (!config) {
    return result;
  }
  result.start_function_call_token = config["start_function_call_token"].as<std::string>();
  result.function_token_prefixes = config["function_token_prefixes"].as<std::vector<std::string>>();
  result.arg_separator = config["arg_separator"].as<std::string>();
  result.stop_markers = config["stop_markers"].as<std::vector<std::string>>();
  result.parallel_tool_calls = config["parallel_tool_calls"].as<bool>(result.parallel_tool_calls);
  if (const auto keep = config["keep_tags"]; keep) {
    if (!keep.IsSequence()) {
      throw std::invalid_argument("Compact keep_tags must be a list");
    }
    result.keep_tags = keep.as<std::vector<std::string>>();
  }
  std::map<std::string, CompactArgFormat> formats;
  for (const auto& item : config["arg_formats"]) {
    formats[item.first.as<std::string>()] = ParseArgFormat(item.second);
  }
  for (const auto& spec_node : config["function_specs"]) {
    CompactFunctionSpec spec;
    spec.name = spec_node["name"].as<std::string>();
    spec.token = spec_node["token"].as<std::string>();
    for (const auto& arg_node : spec_node["args"]) {
      CompactArgSpec arg;
      if (arg_node.IsScalar()) {
        arg.name = arg_node.as<std::string>();
      } else {
        arg.name = arg_node["name"].as<std::string>();
        if (const auto format = arg_node["format"]) {
          const auto it = formats.find(format.as<std::string>());
          if (it == formats.end()) {
            throw std::invalid_argument("Unknown compact argument format: " + format.as<std::string>());
          }
          arg.format = it->second;
        }
        arg.optional = arg_node["optional"].as<bool>(arg.optional);
      }
      if (!arg.optional && !spec.args.empty() && spec.args.back().optional) {
        throw std::invalid_argument("Only trailing compact arguments can be optional: " + spec.name);
      }
      spec.args.push_back(std::move(arg));
    }
    result.function_specs.push_back(std::move(spec));
  }
  return result;
}

CompactToolCallParser::CompactToolCallParser(CompactToolCallParserParams params)
  : params_(std::move(params)) {
  if (params_.function_specs.empty()) {
    throw std::invalid_argument("Compact tool call parser needs function_specs");
  }
  if (params_.parallel_tool_calls && params_.start_function_call_token.empty()) {
    throw std::invalid_argument("Parallel compact tool calls need start_function_call_token");
  }
  for (size_t i = 0; i < params_.function_specs.size(); ++i) {
    const auto& spec = params_.function_specs[i];
    if (spec.name.empty() || spec.token.empty() || !by_token_.emplace(spec.token, i).second) {
      throw std::invalid_argument("Compact function spec needs a unique name and token: " + spec.token);
    }
  }
  for (const auto& tag : params_.keep_tags) {
    const bool reserved = tag == params_.start_function_call_token || tag == params_.arg_separator
      || by_token_.count(tag) > 0
      || std::any_of(params_.function_token_prefixes.begin(),
                     params_.function_token_prefixes.end(),
                     [&tag](const std::string& prefix) {
                       return !prefix.empty() && tag.rfind(prefix, 0) == 0;
                     })
      || std::find(params_.stop_markers.begin(), params_.stop_markers.end(), tag) != params_.stop_markers.end();
    if (!std::regex_match(tag, KeepTagPattern()) || reserved) {
      throw std::invalid_argument("Compact keep_tags entry must be a plain answer tag: " + tag);
    }
  }
}

bool CompactToolCallParser::IsFunctionTokenAt(std::string_view text, size_t at) const {
  for (const auto& prefix : params_.function_token_prefixes) {
    if (!prefix.empty() && text.compare(at, prefix.size(), prefix) == 0) {
      return text.find('>', at) != std::string_view::npos;
    }
  }
  return std::any_of(by_token_.begin(), by_token_.end(), [&](const auto& token) {
    return text.compare(at, token.first.size(), token.first) == 0;
  });
}

std::optional<size_t> CompactToolCallParser::FindStart(std::string_view text) const {
  for (size_t at = text.find('<'); at != std::string_view::npos; at = text.find('<', at + 1)) {
    const auto& start = params_.start_function_call_token;
    if ((!start.empty() && text.compare(at, start.size(), start) == 0) || IsFunctionTokenAt(text, at)) {
      return at;
    }
  }
  return std::nullopt;
}

std::optional<size_t> CompactToolCallParser::FindEnd(std::string_view call_text, bool is_final) const {
  std::optional<size_t> end;
  for (const auto& marker : params_.stop_markers) {
    const auto at = marker.empty() ? std::string_view::npos : call_text.find(marker);
    if (at != std::string_view::npos && (!end || at + marker.size() < *end)) {
      end = at + marker.size();
    }
  }
  return end || !is_final ? end : std::optional<size_t>(call_text.size());
}

size_t CompactToolCallParser::PartialStartLength(std::string_view text) const {
  size_t hold = std::max(UnclosedTagSuffix(text), PartialMarkerSuffix(text, params_.start_function_call_token));
  for (const auto& prefix : params_.function_token_prefixes) {
    hold = std::max(hold, PartialMarkerSuffix(text, prefix));
  }
  for (const auto& [token, index] : by_token_) {
    hold = std::max(hold, PartialMarkerSuffix(text, token));
  }
  return hold;
}

std::vector<ToolCall> CompactToolCallParser::Parse(std::string_view call_text) const {
  return ParseAll(call_text, nullptr);
}

std::vector<std::vector<RawTextSource>> CompactToolCallParser::RawTextSources(
  std::string_view call_text, const std::vector<ToolCall>& calls
) const {
  if (!params_.parallel_tool_calls) {
    return IToolCallParser::RawTextSources(call_text, calls);
  }
  std::vector<std::vector<RawTextSource>> sources;
  ParseAll(call_text, &sources);
  return sources;
}

std::vector<ToolCall> CompactToolCallParser::ParseAll(
  std::string_view call_text, std::vector<std::vector<RawTextSource>>* sources
) const {
  std::string_view text = call_text;
  for (const auto& marker : params_.stop_markers) {
    if (const auto at = marker.empty() ? std::string_view::npos : text.find(marker); at != std::string_view::npos) {
      text = text.substr(0, at);
    }
  }
  const std::string_view body = TrimView(text);
  const auto& start = params_.start_function_call_token;
  const auto next_start = [&](size_t from) {
    if (start.empty()) {
      return body.size();
    }
    const size_t skip = body.compare(from, start.size(), start) == 0 ? start.size() : 0;
    return std::min(body.find(start, from + skip), body.size());
  };
  if (!params_.parallel_tool_calls) {
    // Without the batch mode only the first call of a generation is executed.
    auto call = ParseCall(body.substr(0, next_start(0)), nullptr);
    if (!call) {
      return {};
    }
    call->raw_text = std::string(call_text);
    return {std::move(*call)};
  }
  std::vector<ToolCall> calls;
  for (size_t from = 0; from < body.size();) {
    const size_t next = next_start(from);
    std::vector<std::pair<size_t, std::string_view>> values;
    if (auto call = ParseCall(TrimView(body.substr(from, next - from)), &values)) {
      calls.push_back(std::move(*call));
      if (sources != nullptr) {
        auto& pieces = sources->emplace_back();
        for (const auto& [raw_begin, value] : values) {
          pieces.push_back({raw_begin, static_cast<size_t>(value.data() - call_text.data()), value.size()});
        }
      }
    }
    from = next;
  }
  return calls;
}

std::optional<ToolCall> CompactToolCallParser::ParseCall(
  std::string_view body, std::vector<std::pair<size_t, std::string_view>>* sources
) const {
  size_t pos = 0;
  const auto& start = params_.start_function_call_token;
  if (!start.empty() && body.compare(0, start.size(), start) == 0) {
    pos = start.size();
  }
  const auto close = body.find('>', pos);
  if (close == std::string_view::npos) {
    return std::nullopt;
  }
  const auto found = by_token_.find(std::string(body.substr(pos, close + 1 - pos)));
  if (found == by_token_.end()) {
    return std::nullopt;
  }
  const auto& spec = params_.function_specs[found->second];
  std::vector<Slot> slots(spec.args.size());
  auto arguments = ParseArguments(spec, body.substr(close + 1), slots);
  if (!arguments) {
    return std::nullopt;
  }
  ToolCall call;
  call.name = spec.name;
  call.arguments_json = std::move(*arguments);
  call.raw_text = start + spec.token;
  for (size_t i = 0; i < slots.size(); ++i) {
    call.raw_text += i > 0 ? params_.arg_separator : "";
    if (sources != nullptr && !slots[i].source.empty()) {
      sources->emplace_back(call.raw_text.size(), slots[i].source);
    }
    call.raw_text += slots[i].text;
  }
  return call;
}

// Arguments go in the order of the spec; tags are recognized by their shape, so a missing separator does no harm.
std::optional<std::string> CompactToolCallParser::ParseArguments(
  const CompactFunctionSpec& spec, std::string_view text, std::vector<Slot>& slots
) const {
  const auto& separator = params_.arg_separator;
  std::string json = "{";
  size_t pos = 0;
  for (size_t i = 0; i < spec.args.size(); ++i) {
    const auto& arg = spec.args[i];
    SkipSpaces(text, pos);
    const bool separated = i > 0 && !separator.empty() && text.compare(pos, separator.size(), separator) == 0;
    if (separated) {
      pos += separator.size();
      SkipSpaces(text, pos);
    }
    std::string value;
    if (auto tag = TakeTag(text, pos, arg.format)) {
      value = JsonQuote(tag->value);
      slots[i] = {std::move(tag->canonical), {}};
    } else {
      if (pos >= text.size() && !separated) {
        if (arg.optional) {
          slots.resize(i);
          break;
        }
        return std::nullopt;
      }
      const bool last = i + 1 == spec.args.size();
      auto end = last || separator.empty() ? text.size() : text.find(separator, pos);
      // Only optional ones follow: without a separator the value runs to the end of the call if it has none of
      // their tags.
      const auto tagged = [&](const CompactArgSpec& next) {
        return HasTag(text.substr(pos), next.format);
      };
      if (
        end == std::string_view::npos && spec.args[i + 1].optional
        && std::none_of(spec.args.begin() + static_cast<std::ptrdiff_t>(i) + 1, spec.args.end(), tagged)
      ) {
        end = text.size();
      }
      if (end == std::string_view::npos) {
        return std::nullopt;
      }
      const auto plain = PlainValue(text.substr(pos, end - pos));
      value = arg.format.as_string ? JsonQuote(plain) : JsonScalar(std::string(plain));
      slots[i] = {std::string(plain), plain};
      pos = end;
    }
    json += (i > 0 ? "," : "") + JsonQuote(arg.name) + ":" + value;
  }
  if (!Trim(text.substr(pos)).empty()) {
    return std::nullopt;
  }
  return json + "}";
}

std::string CompactToolCallParser::CleanText(std::string_view text) const {
  std::string out(text);
  for (const auto& marker : params_.stop_markers) {
    if (marker.empty()) {
      continue;
    }
    for (auto at = out.find(marker); at != std::string::npos; at = out.find(marker, at)) {
      out.erase(at, marker.size());
    }
  }
  if (params_.keep_tags.empty()) {
    return std::regex_replace(out, TagPattern(), "");
  }
  if (out.find('<') == std::string::npos) {
    return out;
  }
  std::string kept;
  size_t pos = 0;
  for (auto it = std::sregex_iterator(out.begin(), out.end(), KeepTagPattern()); it != std::sregex_iterator(); ++it) {
    const auto at = static_cast<size_t>(it->position());
    kept.append(out, pos, at - pos);
    if (std::find(params_.keep_tags.begin(), params_.keep_tags.end(), it->str()) != params_.keep_tags.end()) {
      kept += it->str();
    }
    pos = at + static_cast<size_t>(it->length());
  }
  kept.append(out, pos, std::string::npos);
  return kept;
}

} // namespace giga_edge
