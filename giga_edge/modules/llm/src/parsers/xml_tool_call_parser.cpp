#include "giga_edge/modules/llm/parsers/xml_tool_call_parser.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/modules/assistant_core/json.h"

namespace giga_edge {

XmlToolCallParserParams ParseXmlToolCallParserParams(const YAML::Node& config) {
  XmlToolCallParserParams result;
  if (!config) {
    return result;
  }
  result.call_open = config["call_open"].as<std::string>(result.call_open);
  result.call_close = config["call_close"].as<std::string>(result.call_close);
  return result;
}

XmlToolCallParser::XmlToolCallParser(XmlToolCallParserParams params)
  : params_(std::move(params)) {}

std::optional<size_t> XmlToolCallParser::FindStart(std::string_view text) const {
  const auto at = text.find(params_.call_open);
  return at == std::string_view::npos ? std::nullopt : std::optional<size_t>(at);
}

std::optional<size_t> XmlToolCallParser::FindEnd(std::string_view call_text, bool is_final) const {
  const auto at = call_text.find(params_.call_close, params_.call_open.size());
  if (at != std::string_view::npos) {
    return at + params_.call_close.size();
  }
  return is_final ? std::optional<size_t>(call_text.size()) : std::nullopt;
}

size_t XmlToolCallParser::PartialStartLength(std::string_view text) const {
  return PartialMarkerSuffix(text, params_.call_open);
}

std::vector<ToolCall> XmlToolCallParser::Parse(std::string_view call_text) const {
  auto body = call_text.substr(params_.call_open.size());
  if (
    body.size() >= params_.call_close.size()
    && body.substr(body.size() - params_.call_close.size()) == params_.call_close
  ) {
    body = body.substr(0, body.size() - params_.call_close.size());
  }
  // The call body is strict JSON: the name and the arguments at the top level only.
  Json::Value parsed;
  std::string error;
  if (!ParseJson(body, parsed, error) || !parsed.isObject()) {
    return {};
  }
  const Json::Value& call_json = parsed;
  const Json::Value& name = call_json["name"];
  const Json::Value& arguments = call_json["arguments"];
  if (!name.isString() || name.asString().empty() || !(arguments.isNull() || arguments.isObject())) {
    return {};
  }
  ToolCall call;
  call.name = name.asString();
  if (arguments.isObject()) {
    // The arguments are the model's bytes as is.
    call.arguments_json = std::string(body.substr(
      static_cast<size_t>(arguments.getOffsetStart()),
      static_cast<size_t>(arguments.getOffsetLimit() - arguments.getOffsetStart())
    ));
  }
  call.raw_text = std::string(call_text);
  return {std::move(call)};
}

std::string XmlToolCallParser::CleanText(std::string_view text) const {
  return std::string(text);
}

} // namespace giga_edge
