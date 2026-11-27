#pragma once

#include <json/json.h>

#include <string>
#include <string_view>

namespace giga_edge {

// Strict parsing: trailing data after the value and duplicate keys are errors.
bool ParseJson(std::string_view text, Json::Value& out, std::string& error);
Json::Value ParseJsonOrThrow(std::string_view text);
// Non-ASCII is written as UTF-8 with any jsoncpp version.
std::string WriteJson(const Json::Value& value);
std::string JsonQuote(std::string_view text);
std::string KeepJsonUtf8(std::string json);

} // namespace giga_edge
