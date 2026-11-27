#include "giga_edge/modules/smart_home_ha/actions.h"

#include "giga_edge/modules/assistant_core/json.h"
#include "giga_edge/modules/smart_home_ha/colour.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <vector>

namespace giga_edge::ha {
namespace {
constexpr auto kRead = "homeassistant__GetLiveContext";
constexpr auto kHumiditySetpoint = "humidifier__HassHumidifierSetpoint";

McpResolvedAction Failure(const std::string& message, const ToolCall* call) {
  McpResolvedAction out;
  out.status = AssistantStatus::ERROR;
  out.error = message;
  if (call) {
    out.call_id = call->id;
    out.source_action_name = call->name;
  }
  return out;
}
Json::Value Context(
  const Device& d, const std::string& alias, const std::string& parameter, const std::string& operation
) {
  Json::Value out;
  out["alias"] = alias;
  out["name"] = d.name;
  out["domain"] = d.domain;
  out["area"] = d.area;
  out["parameter"] = parameter;
  out["operation"] = operation;
  if (!d.tool.empty()) {
    out["tool"] = d.tool;
  }
  return out;
}
Json::Value Target(const Json::Value& context, bool read) {
  Json::Value out;
  out["name"] = context["name"];
  if (!context["area"].asString().empty()) {
    out["area"] = context["area"];
  }
  if (read) {
    out["domain"] = context["domain"];
  } else if (context["domain"] != "climate" || context["parameter"] == "on_off") {
    // HassClimateSetTemperature has no domain slot; HassTurnOn/Off do.
    out["domain"].append(context["domain"]);
  }
  return out;
}
double Numeric(Json::Value value, bool allow_percent) {
  if (value.isNumeric()) {
    const auto number = value.asDouble();
    if (std::isfinite(number)) {
      return number;
    }
  } else if (value.isString()) {
    auto text = value.asString();
    if (!text.empty() && text.back() == '%') {
      if (!allow_percent) {
        throw std::runtime_error("PERCENTAGE_NOT_ALLOWED");
      }
      text.pop_back();
    }
    if (const auto number = ParseNumber(text)) {
      return *number;
    }
  }
  throw std::runtime_error("BAD_VALUE");
}
// Readable percent parameters: relative changes step by 10 and clamp to 0..100.
bool IsPercent(const std::string& parameter) {
  return parameter == "light_brightness" || parameter == "open_percentage" || parameter == "volume_int"
    || parameter == "hvac_humidity_set" || parameter == "light_colour_temp";
}
double Percent(const Json::Value& value) {
  const auto number = Numeric(value, true);
  if (number < 0 || number > 100) {
    throw std::runtime_error("BAD_VALUE");
  }
  return number;
}
bool Boolean(const Json::Value& value) {
  if (value.isBool()) {
    return value.asBool();
  }
  if (value.isString()) {
    const auto s = value.asString();
    if (s == "true" || s == "<true>") {
      return true;
    }
    if (s == "false" || s == "<false>") {
      return false;
    }
  }
  throw std::runtime_error("BAD_VALUE");
}
void MakeWrite(McpResolvedAction& action, Json::Value context, const Json::Value& value) {
  auto args = Target(context, false);
  const auto parameter = context["parameter"].asString();
  if (parameter == "on_off") {
    const bool on = Boolean(value);
    if (context["domain"] == "vacuum") {
      action.tool_name = on ? "vacuum__HassVacuumStart" : "vacuum__HassVacuumReturnToBase";
    } else if (on && context.isMember("tool")) {
      // A script starts by its own tool, which takes no target; it is stopped as any entity is switched off.
      action.tool_name = context["tool"].asString();
      args = Json::Value(Json::objectValue);
    } else {
      action.tool_name = on ? "intent__HassTurnOn" : "intent__HassTurnOff";
    }
  } else if (parameter == "vacuum_cleaner_status") {
    const auto status = value.isString() ? value.asString() : std::string();
    if (status == "cleaning") {
      action.tool_name = "vacuum__HassVacuumStart";
    } else if (status == "docked" || status == "returning_to_dock") {
      action.tool_name = "vacuum__HassVacuumReturnToBase";
    } else {
      throw std::runtime_error("BAD_VALUE");
    }
  } else if (parameter == "light_brightness") {
    action.tool_name = "light__HassLightSet";
    args["brightness"] = static_cast<int>(std::lround(Percent(value)));
  } else if (parameter == "light_colour") {
    if (!value.isString()) {
      throw std::runtime_error("BAD_VALUE");
    }
    action.tool_name = "light__HassLightSet";
    args["color"] = ColourName(value.asString());
  } else if (parameter == "hvac_temp_set") {
    action.tool_name = "climate__HassClimateSetTemperature";
    args["temperature"] = Numeric(value, false);
  } else if (parameter == "hvac_air_flow_power") {
    // The lowest percentage that HA's percentage_to_ordered_list_item reads back as this speed: a fan rounds it up to
    // a speed of its own (math.ceil in the fan docs of HA), so with 5 speeds or more it reads back the same.
    const auto& speeds = FanSpeeds();
    const auto speed = std::find(speeds.begin(), speeds.end(), value.isString() ? value.asString() : std::string());
    if (speed == speeds.end()) {
      throw std::runtime_error("BAD_VALUE");
    }
    action.tool_name = "fan__HassFanSetSpeed";
    args["percentage"] = static_cast<int>((speed - speeds.begin()) * 100 / speeds.size() + 1);
  } else if (parameter == "hvac_humidity_set") {
    // Its slots are the name and the humidity: Build makes it writable only for a name unique in the house.
    action.tool_name = kHumiditySetpoint;
    args = Json::Value(Json::objectValue);
    args["name"] = context["name"];
    args["humidity"] = static_cast<int>(std::lround(Percent(value)));
  } else if (parameter == "light_colour_temp") {
    // The model writes the colour temperature in percent of the scale it was trained on: 0 is the warmest, 2700 K,
    // 100 the coldest, 6500 K. HA does not report the range of the lamp, so the scale is the typical one; HA clamps
    // it to the lamp.
    action.tool_name = "light__HassLightSet";
    args["temperature"] = static_cast<int>(std::lround(2700 + Percent(value) * (6500 - 2700) / 100));
  } else if (parameter == "open_percentage") {
    action.tool_name = "intent__HassSetPosition";
    args["position"] = static_cast<int>(std::lround(Percent(value)));
  } else if (parameter == "open_set") {
    // The open/close/stop values follow the description of the open_set parameter in the catalog format.
    const auto command = value.isString() ? value.asString() : std::string();
    if (command == "open" || command == "close") {
      action.tool_name = command == "open" ? "intent__HassTurnOn" : "intent__HassTurnOff";
    } else if (command == "stop") {
      action.tool_name = "intent__HassStopMoving";
    } else {
      throw std::runtime_error("BAD_VALUE");
    }
  } else if (parameter == "volume_int") {
    action.tool_name = "media_player__HassSetVolume";
    args["volume_level"] = static_cast<int>(std::lround(Percent(value)));
  } else if (parameter == "mute") {
    action.tool_name = Boolean(value) ? "media_player__HassMediaPlayerMute" : "media_player__HassMediaPlayerUnmute";
  } else {
    throw std::runtime_error("PARAMETER_READ_ONLY");
  }
  context["operation"] = "set";
  context.removeMember("delta");
  context.removeMember("value");
  action.kind = McpResolvedActionKind::CALL_TOOL;
  action.arguments_json = WriteJson(args);
  action.context_json = WriteJson(context);
}
void PendingWrite(McpResolvedAction& action, Json::Value context, const Json::Value& value) {
  if (context.isMember("tool")) {
    MakeWrite(action, std::move(context), value);
    return;
  }
  context["operation"] = "set_pending";
  context["value"] = value;
  action.kind = McpResolvedActionKind::CALL_TOOL;
  action.tool_name = kRead;
  action.arguments_json = WriteJson(Target(context, true));
  action.context_json = WriteJson(context);
}
bool SameDevice(const Device& d, const Json::Value& context) {
  return d.name == context["name"].asString() && d.domain == context["domain"].asString()
    && d.area == context["area"].asString();
}
Json::Value Unwrap(Json::Value value, unsigned depth = 0) {
  if (depth > 8) {
    throw std::runtime_error("DEVICE_UNREACHABLE");
  }
  if (value.isString()) {
    return Unwrap(ParseJsonOrThrow(value.asString()), depth + 1);
  }
  if (value.isArray()) {
    if (value.size() != 1) {
      throw std::runtime_error("DEVICE_UNREACHABLE");
    }
    return Unwrap(value[0], depth + 1);
  }
  if (!value.isObject()) {
    throw std::runtime_error("DEVICE_UNREACHABLE");
  }
  if (value.isMember("text")) {
    return Unwrap(value["text"], depth + 1);
  }
  if (value.isMember("content")) {
    return Unwrap(value["content"], depth + 1);
  }
  return value;
}
std::string ModelError(const std::string& text) {
  static const std::set<std::string> known = {
    "UNKNOWN_FUNCTION",
    "UNKNOWN_PARAMETER",
    "PARAMETER_READ_ONLY",
    "RELATIVE_NOT_ALLOWED",
    "PERCENTAGE_NOT_ALLOWED",
    "BAD_VALUE",
    "MISSING_ARGS"
  };
  const auto code = text.substr(0, text.find(':'));
  return known.count(code) ? code : "DEVICE_UNREACHABLE";
}
} // namespace

McpResolvedAction ResolveAction(const AssistantResult& result, const Catalog& catalog) {
  if (
    !result.is_final || result.status != AssistantStatus::OK || result.tool_calls.empty()
    || result.finish_reason == StreamFinishReason::CANCELLED || result.finish_reason == StreamFinishReason::MAX_TOKENS
    || result.finish_reason == StreamFinishReason::ERROR
  ) {
    return {.status = AssistantStatus::SKIPPED};
  }
  const auto& call = result.tool_calls.front();
  if (result.tool_calls.size() != 1) {
    return Failure("Expected one HA action", &call);
  }
  if (catalog.status == AssistantStatus::ERROR) {
    return Failure(catalog.error, &call);
  }
  try {
    const bool get = call.name == "get_device_parameter";
    const bool relative = call.name == "relative_change";
    const bool copy = call.name == "copy_device_parameter";
    if (!get && !relative && !copy && call.name != "set_device_parameter") {
      throw std::runtime_error("UNKNOWN_FUNCTION");
    }
    const auto payload = ParseJsonOrThrow(call.arguments_json);
    if (!payload.isObject() || !payload["device_id"].isString() || !payload["parameter_name"].isString()) {
      throw std::runtime_error("MISSING_ARGS");
    }
    const auto alias = payload["device_id"].asString();
    const auto parameter = payload["parameter_name"].asString();
    const auto device = catalog.devices.find(alias);
    if (device == catalog.devices.end()) {
      throw std::runtime_error("DEVICE_UNREACHABLE");
    }
    const auto& d = device->second;
    const auto feature = d.features.find(parameter);
    if (feature == d.features.end()) {
      throw std::runtime_error("UNKNOWN_PARAMETER");
    }
    if (!get && !feature->second.writable) {
      throw std::runtime_error("PARAMETER_READ_ONLY");
    }

    // The context does not list scripts, so there is nothing to read either.
    if (get && !d.tool.empty()) {
      throw std::runtime_error("UNKNOWN_PARAMETER");
    }

    McpResolvedAction action;
    action.kind = McpResolvedActionKind::CALL_TOOL;
    action.call_id = call.id;
    action.source_action_name = call.name;
    action.server_id = d.server_id;
    auto context = Context(d, alias, parameter, relative ? "relative" : get ? "get" : "set");
    if (copy) {
      if (!payload["source_device_id"].isString()) {
        throw std::runtime_error("MISSING_ARGS");
      }
      const auto source_alias = payload["source_device_id"].asString();
      const auto source = catalog.devices.find(source_alias);
      if (source == catalog.devices.end()) {
        throw std::runtime_error("DEVICE_UNREACHABLE");
      }
      // HA reports no colour, but another server may: the fresh read of the source decides.
      const bool colour = parameter == "light_colour" || parameter == "light_colour_temp";
      if (!source->second.features.count(parameter) || (WriteOnly(source->second, parameter) && !colour)) {
        throw std::runtime_error("UNKNOWN_PARAMETER");
      }
      auto source_context = Context(source->second, source_alias, parameter, "copy");
      source_context["destination_server"] = d.server_id;
      source_context["destination"] = std::move(context);
      context = std::move(source_context);
      action.server_id = source->second.server_id;
    } else if (relative) {
      const bool percent = IsPercent(parameter);
      const bool speed = parameter == "hvac_air_flow_power";
      if (!percent && !speed && parameter != "hvac_temp_set") {
        throw std::runtime_error("RELATIVE_NOT_ALLOWED");
      }
      const auto& value = payload["value"];
      const auto text = value.isString() ? value.asString() : std::string();
      // A speed steps by one along the speeds: there is no number to add to it.
      if (speed && text != "+" && text != "-") {
        throw std::runtime_error("BAD_VALUE");
      }
      context["delta"] = text == "+" || text == "-"
        ? (percent ? 10.0 : 1.0) * (text == "-" ? -1 : 1)
        : Numeric(value, percent);
      // For the model a step up of the colour temperature is warmer, that is a smaller percent: 0 is the warmest.
      if (parameter == "light_colour_temp") {
        context["delta"] = -context["delta"].asDouble();
      }
    } else if (!get) {
      if (!payload.isMember("value")) {
        throw std::runtime_error("MISSING_ARGS");
      }
      // Validate user input and the write tool before reading the target.
      MakeWrite(action, context, payload["value"]);
      if (!catalog.tools.at(d.server_id).count(action.tool_name)) {
        throw std::runtime_error("DEVICE_UNREACHABLE");
      }
      PendingWrite(action, context, payload["value"]);
      if (!catalog.tools.at(action.server_id).count(action.tool_name)) {
        throw std::runtime_error("DEVICE_UNREACHABLE");
      }
      return action;
    }
    // Every operation starts with a fresh read; writes follow as its continuation.
    action.tool_name = kRead;
    action.arguments_json = WriteJson(Target(context, true));
    action.context_json = WriteJson(context);
    if (!catalog.tools.at(action.server_id).count(action.tool_name)) {
      throw std::runtime_error("DEVICE_UNREACHABLE");
    }
    return action;
  } catch (const std::exception& error) {
    return Failure(error.what(), &call);
  }
}

bool NeedsContinuation(const McpResolvedAction& action) {
  if (action.tool_name != kRead || action.context_json.empty()) {
    return false;
  }
  try {
    const auto operation = ParseJsonOrThrow(action.context_json)["operation"];
    return operation == "relative" || operation == "set_pending" || operation == "copy";
  } catch (const std::exception&) {
    return false;
  }
}

bool MatchesTarget(const McpResolvedAction& action, const Catalog& catalog) {
  try {
    if (catalog.status != AssistantStatus::OK) {
      return false;
    }
    const auto context = ParseJsonOrThrow(action.context_json);
    const auto d = catalog.devices.find(context["alias"].asString());
    if (
      d == catalog.devices.end() || d->second.server_id != action.server_id || !SameDevice(d->second, context)
      || !catalog.tools.at(action.server_id).count(action.tool_name)
    ) {
      return false;
    }
    const auto feature = d->second.features.find(context["parameter"].asString());
    return feature != d->second.features.end() && (context["operation"] == "get" || feature->second.writable);
  } catch (const std::exception&) {
    return false;
  }
}

Completion Complete(const McpActionCompletion& completion) {
  Completion out;
  const auto& action = completion.action;
  CompactToolResult result;
  result.call_id = action.call_id;
  result.name = action.source_action_name;
  try {
    if (completion.is_error) {
      throw std::runtime_error(completion.error);
    }
    const auto& raw = completion.structured_content_json.empty()
      ? completion.content_json
      : completion.structured_content_json;
    if (raw.size() > 262144) {
      throw std::runtime_error("HA reply exceeds context limit");
    }
    const auto payload = ParseJsonOrThrow(raw);
    const auto context = ParseJsonOrThrow(action.context_json);
    if (action.tool_name == kRead) {
      const auto devices = ParseContext(payload, action.server_id);
      // GetLiveContext uses HA's own alias matching and returns every match.
      // Never ignore an additional alias match just because its primary name differs.
      if (devices.size() != 1) {
        throw std::runtime_error("Ambiguous HA read");
      }
      const Device* matched = &devices[0];
      if (!SameDevice(*matched, context)) {
        throw std::runtime_error("DEVICE_UNREACHABLE");
      }
      if (context["operation"] == "set_pending") {
        // A scene is only activated: once it is found, there is nothing left to switch off.
        if (context["domain"] == "scene" && !Boolean(context["value"])) {
          out.results.push_back(std::move(result));
          return out;
        }
        auto next = action;
        MakeWrite(next, context, context["value"]);
        out.continuations.push_back(std::move(next));
        return out;
      }
      const auto parameter = context["parameter"].asString();
      if (!matched->online && parameter != "online") {
        throw std::runtime_error("DEVICE_UNREACHABLE");
      }
      const auto it = matched->features.find(parameter);
      if (it == matched->features.end() || it->second.value.isNull()) {
        throw std::runtime_error("UNKNOWN_PARAMETER");
      }
      if (context["operation"] == "copy") {
        auto next = action;
        next.server_id = context["destination_server"].asString();
        PendingWrite(next, context["destination"], it->second.value);
        out.continuations.push_back(std::move(next));
        return out;
      }
      if (context["operation"] == "relative" && parameter == "hvac_air_flow_power") {
        const auto& speeds = FanSpeeds();
        const auto at = std::find(speeds.begin(), speeds.end(), it->second.value.asString()) - speeds.begin();
        const auto to = std::clamp<std::ptrdiff_t>(
          at + static_cast<std::ptrdiff_t>(context["delta"].asDouble()),
          0,
          static_cast<std::ptrdiff_t>(speeds.size()) - 1
        );
        auto next = action;
        MakeWrite(next, context, speeds[to]);
        out.continuations.push_back(std::move(next));
        return out;
      }
      if (context["operation"] == "relative") {
        if (!it->second.value.isNumeric()) {
          throw std::runtime_error("BAD_VALUE");
        }
        double value = it->second.value.asDouble() + context["delta"].asDouble();
        if (!std::isfinite(value)) {
          throw std::runtime_error("BAD_VALUE");
        }
        if (IsPercent(parameter)) {
          value = std::clamp(value, 0.0, 100.0);
        }
        auto next = action;
        MakeWrite(next, context, value);
        out.continuations.push_back(std::move(next));
        return out;
      }
      // The model says the value it reads: a colour word, not the RGB triplet.
      result.value_json = WriteJson(
        parameter == "light_colour" ? Json::Value(SpokenColour(it->second.value.asString())) : it->second.value
      );
    } else if (action.tool_name == context["tool"].asString()) {
      // A script replies with its response, {"result": {...}}, once it has run: there is no target to confirm.
      if (!Unwrap(payload).isMember("result")) {
        throw std::runtime_error("HA did not confirm the action");
      }
    } else {
      const auto value = Unwrap(payload);
      const auto& data = value["data"];
      if (
        value["response_type"] != "action_done" || !data["success"].isArray() || !data["failed"].isArray()
        || !data["failed"].empty()
      ) {
        throw std::runtime_error("HA did not confirm the action");
      }
      // HassHumidifierSetpoint lists no targets: it fails unless the one humidifier it matched by the name is set.
      if (action.tool_name == kHumiditySetpoint && data["success"].empty()) {
        out.results.push_back(std::move(result));
        return out;
      }
      // Besides the entity, HA lists the requested targets in success: for a device with an area it is [area, entity].
      std::vector<const Json::Value*> entities;
      for (const auto& target : data["success"]) {
        if (target["type"] == "entity") {
          entities.push_back(&target);
        }
      }
      if (
        entities.size() != 1 || !(*entities[0])["id"].isString() || (*entities[0])["id"].asString().empty()
        || (*entities[0])["name"] != context["name"]
      ) {
        throw std::runtime_error("HA did not confirm the target");
      }
    }
  } catch (const std::exception& error) {
    result.is_error = true;
    result.error = ModelError(error.what());
  }
  out.results.push_back(std::move(result));
  return out;
}
} // namespace giga_edge::ha
