#include "giga_edge/modules/function_calling/compact_tool_result.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/modules/assistant_core/json.h"
#include "giga_edge/modules/function_calling/nodes/fc_tool_result/node.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace giga_edge {
namespace {

// Round the decimal spelling, not a binary double (2.675 must become 2.68).
std::string RoundDecimal(std::string_view number, int decimals) {
  const bool negative = number.front() == '-';
  auto mantissa = number.substr(negative ? 1 : 0);
  int exponent = 0;
  if (const auto at = mantissa.find_first_of("eE"); at != std::string_view::npos) {
    auto power = mantissa.substr(at + 1);
    if (!power.empty() && power.front() == '+') {
      power.remove_prefix(1);
    }
    if (std::from_chars(power.data(), power.data() + power.size(), exponent).ec != std::errc{}) {
      return std::string(number);
    }
    mantissa = mantissa.substr(0, at);
  }
  const auto dot = mantissa.find('.');
  int64_t point = static_cast<int64_t>(dot == std::string_view::npos ? mantissa.size() : dot) + exponent;
  std::string digits(mantissa);
  digits.erase(std::remove(digits.begin(), digits.end(), '.'), digits.end());
  const int64_t keep = point + decimals;
  if (keep < 0) {
    return "0";
  }
  if (keep < static_cast<int64_t>(digits.size())) {
    const bool up = digits[keep] >= '5';
    digits.resize(static_cast<size_t>(keep));
    if (up) {
      auto i = digits.size();
      while (i > 0 && digits[i - 1] == '9') {
        digits[--i] = '0';
      }
      if (i > 0) {
        ++digits[i - 1];
      } else {
        digits.insert(digits.begin(), '1');
        ++point;
      }
    }
  }
  while (!digits.empty() && digits.back() == '0') {
    digits.pop_back();
  }
  if (digits.empty()) {
    return "0";
  }
  const auto first = digits.find_first_not_of('0');
  digits.erase(0, first);
  point -= static_cast<int64_t>(first);
  // Keep the original spelling when fixed notation would need more than 128 digits.
  const int64_t size = point > 0
    ? std::max(point, static_cast<int64_t>(digits.size()))
    : 1 - point + static_cast<int64_t>(digits.size());
  if (size > 128) {
    return std::string(number);
  }
  if (point <= 0) {
    digits = "0." + std::string(static_cast<size_t>(-point), '0') + digits;
  } else if (point < static_cast<int64_t>(digits.size())) {
    digits.insert(static_cast<size_t>(point), 1, '.');
  } else {
    digits.resize(static_cast<size_t>(point), '0');
  }
  return negative ? "-" + digits : digits;
}

// The JSON has already been validated. Preserve all bytes except real-number tokens, at any nesting depth.
std::string RoundValues(const std::string& json, int decimals) {
  std::string out;
  for (size_t i = 0; i < json.size();) {
    const size_t start = i;
    if (json[i] == '"') {
      ++i;
      while (i < json.size() && json[i] != '"') {
        i += json[i] == '\\' ? 2 : 1;
      }
      ++i;
      out.append(json, start, i - start);
    } else if (json[i] == '-' || (json[i] >= '0' && json[i] <= '9')) {
      i = json.find_first_not_of("-+0123456789.eE", i);
      if (i == std::string::npos) {
        i = json.size();
      }
      const auto number = std::string_view(json).substr(start, i - start);
      out += number.find_first_of(".eE") == std::string_view::npos
        ? std::string(number)
        : RoundDecimal(number, decimals);
    } else {
      out += json[i++];
    }
  }
  return out;
}

std::string Fill(std::string text, const std::string& key, const std::string& value) {
  if (const auto at = text.find(key); at != std::string::npos) {
    text.replace(at, key.size(), value);
  }
  return text;
}

} // namespace

template<>
CompactResultFormat ParseConfig(const YAML::Node& config) {
  return ParseCompactResultFormat(config);
}

CompactResultFormat ParseCompactResultFormat(const YAML::Node& config) {
  CompactResultFormat format;
  format.success = config["success"].as<std::string>();
  format.success_with_value = config["success_with_value"].as<std::string>();
  format.failure = config["failure"].as<std::string>();
  format.value_decimals = config["value_decimals"].as<int>(format.value_decimals);
  if (format.value_decimals < 0) {
    throw std::invalid_argument("fc_tool_result: value_decimals must be non-negative");
  }
  return format;
}

ToolResult FormatCompactToolResult(const CompactToolResult& input, const CompactResultFormat& format) {
  ToolResult out;
  out.call_id = input.call_id;
  out.name = input.name;
  out.is_ready = true;
  std::string error = input.error;
  Json::Value value;
  const bool failed = input.is_error || (!input.value_json.empty() && !ParseJson(input.value_json, value, error));
  if (failed) {
    out.content = Fill(format.failure, "{error}", JsonQuote(input.is_error ? error : "Invalid tool result JSON"));
  } else if (!input.content.empty()) {
    out.content = input.content;
  } else if (input.value_json.empty()) {
    out.content = format.success;
  } else {
    out.content = Fill(format.success_with_value, "{value}", RoundValues(input.value_json, format.value_decimals));
  }
  return out;
}

void FcToolResultNode::ProcessOnce() {
  for (const auto& result : GetToolResults()) {
    pending_.push_back(FormatCompactToolResult(result, format_));
  }
  if (pending_.empty()) {
    PushToolResult(ToolResult{});
  } else {
    PushToolResult(std::move(pending_.front()));
    pending_.pop_front();
  }
}

} // namespace giga_edge
