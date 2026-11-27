#include "giga_edge/modules/smart_home_ha/catalog.h"

#include "giga_edge/lib/common/str_utils.h"
#include "giga_edge/modules/assistant_core/json.h"
#include "giga_edge/modules/assistant_core/text.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <stdexcept>

namespace giga_edge::ha {
namespace {
std::string ContextText(const Json::Value& value, unsigned depth = 0) {
  if (depth > 8) {
    throw std::runtime_error("Nested HA context");
  }
  if (value.isString()) {
    const auto text = value.asString();
    Json::Value parsed;
    std::string error;
    return ParseJson(text, parsed, error) ? ContextText(parsed, depth + 1) : text;
  }
  if (value.isArray()) {
    if (value.size() != 1) {
      throw std::runtime_error("Ambiguous HA context content");
    }
    return ContextText(value[0], depth + 1);
  }
  if (value.isObject()) {
    for (const auto* key : {"text", "contents", "content", "result", "data"}) {
      if (value.isMember(key)) {
        return ContextText(value[key], depth + 1);
      }
    }
  }
  throw std::runtime_error("Missing HA context text");
}
std::string Scalar(const YAML::Node& node) {
  return node && node.IsScalar() ? node.as<std::string>() : std::string();
}
Json::Value Number(const YAML::Node& node) {
  if (!node || !node.IsScalar()) {
    return {};
  }
  const auto value = ParseNumber(node.as<std::string>());
  return value ? Json::Value(*value) : Json::Value();
}
// The rgb_color list as the model writes a colour, "R;G;B".
Json::Value Rgb(const YAML::Node& node) {
  if (!node || !node.IsSequence() || node.size() != 3) {
    return {};
  }
  std::string out;
  for (const auto& channel : node) {
    const auto value = Number(channel);
    if (
      !value.isNumeric() || value.asDouble() < 0 || value.asDouble() > 255
      || value.asDouble() != std::round(value.asDouble())
    ) {
      return {};
    }
    out += (out.empty() ? "" : ";") + std::to_string(value.asInt());
  }
  return out;
}
// The colour temperature percent of the model from color_temp_kelvin: the inverse of the write, 2700..6500 K is
// 0..100 %.
Json::Value ColourTemp(const YAML::Node& node) {
  const auto kelvin = Number(node);
  return kelvin.isNumeric()
    ? Json::Value(std::clamp(std::round((kelvin.asDouble() - 2700) * 100 / (6500 - 2700)), 0.0, 100.0))
    : Json::Value();
}
std::vector<std::string> Labels(const std::string& text) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start < text.size()) {
    const auto end = text.find(", ", start);
    const auto label = Trim(std::string_view(text).substr(start, end == std::string::npos ? end : end - start));
    if (!label.empty()) {
      out.push_back(label);
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 2;
  }
  return out;
}
std::u16string Key(const std::string& label) {
  return ToLowerLatinCyrillic(ToUtf16(label));
}
std::set<std::u16string> Keys(const std::vector<std::string>& labels) {
  std::set<std::u16string> out;
  for (const auto& label : labels) {
    out.insert(Key(label));
  }
  return out;
}
std::string Identity(const Device& d) {
  Json::Value key(Json::arrayValue);
  // The category is part of the alias, and a cover changes it with its device_class.
  for (const auto& s : {d.server_id, d.domain, d.name, d.area, d.category}) {
    key.append(s);
  }
  return WriteJson(key);
}
std::string Label(std::string text) {
  for (auto& c : text) {
    if (static_cast<unsigned char>(c) < 32 || c == '<' || c == '>' || c == '[' || c == ']') {
      c = ' ';
    }
  }
  return text;
}
// Build decides writability once the server's tools are known.
void Add(Device& d, const std::string& parameter, Json::Value value) {
  d.features[parameter] = {false, d.online ? std::move(value) : Json::Value()};
}
bool HasTool(const Catalog& c, const std::string& server, const std::string& tool) {
  const auto it = c.tools.find(server);
  return it != c.tools.end() && it->second.count(tool);
}
// Categories come only from the vocabulary of the model: a climate sensor is sensor_temp whatever it measures.
// Sensors that report only a battery level are not shown: in the catalog format the battery level is a parameter
// of the device itself.
std::string SensorParameter(const std::string& device_class) {
  if (device_class == "temperature" || device_class == "humidity") {
    return device_class;
  }
  return {};
}
std::string CoverCategory(const std::string& device_class) {
  if (device_class == "blind" || device_class == "shade" || device_class == "shutter" || device_class == "awning") {
    return "window_blind";
  }
  if (device_class == "gate" || device_class == "garage" || device_class == "door") {
    return "gate";
  }
  return "curtain";
}
// The model has no category for a scene or a script: it is shown as a device that is switched on.
constexpr auto kScenarioCategory = "socket";
// HA lists a script only as the tool script__<object id>; its title is the name of the script. A script with
// required fields is not offered: the model has no arguments for it.
std::optional<Device> ScriptDevice(const McpCatalogTool& tool) {
  const std::string prefix = "script__";
  if (tool.tool_name.compare(0, prefix.size(), prefix) != 0 || tool.title.empty()) {
    return std::nullopt;
  }
  Json::Value schema;
  std::string error;
  if (
    !tool.input_schema_json.empty() && ParseJson(tool.input_schema_json, schema, error) && schema.isObject()
    && !schema["required"].empty()
  ) {
    return std::nullopt;
  }
  Device d;
  d.server_id = tool.server_id;
  d.domain = "script";
  d.name = tool.title;
  d.names = {d.name};
  d.category = kScenarioCategory;
  d.tool = tool.tool_name;
  d.features["on_off"] = {};
  return d;
}
// Domains whose state is on/off: anything else (unavailable, unknown) has no value.
Json::Value OnOff(const std::string& state) {
  return state == "on" || state == "off" ? Json::Value(state == "on") : Json::Value();
}
// The state of a climate is its hvac_mode: every mode but off is on.
Json::Value HvacOn(const std::string& state) {
  static const std::set<std::string> modes = {"off", "heat", "cool", "heat_cool", "auto", "dry", "fan_only"};
  return modes.count(state) ? Json::Value(state != "off") : Json::Value();
}
// Binary sensor classes that are the sensor of a model category, and its parameter; on means open, detected or wet.
const std::map<std::string, std::pair<std::string, std::string>>& BinarySensors() {
  static const std::map<std::string, std::pair<std::string, std::string>> sensors = {
    {"door", {"sensor_door", "doorcontact_state"}},
    {"garage_door", {"sensor_door", "doorcontact_state"}},
    {"motion", {"sensor_pir", "pir"}},
    {"smoke", {"sensor_smoke", "smoke_state"}},
    {"gas", {"sensor_gas", "gas_leak_state"}},
    {"moisture", {"sensor_water_leak", "water_leak_state"}},
  };
  return sensors;
}
// HA's percentage_to_ordered_list_item over the speeds; 0 is a fan that is off, which has no speed.
Json::Value FanSpeed(const Json::Value& percentage) {
  const auto& speeds = FanSpeeds();
  if (!percentage.isNumeric() || percentage.asDouble() <= 0) {
    return {};
  }
  for (size_t i = 0; i < speeds.size(); ++i) {
    const size_t upper = (i + 1) * 100 / speeds.size(); // HA divides the bounds of the bands as integers
    if (percentage.asDouble() <= static_cast<double>(upper)) {
      return speeds[i];
    }
  }
  return speeds.back();
}
// A tool of an integration (light__, fan__, humidifier__...) handles only the entities of its domain, intent__ any.
bool ToolFits(const std::string& tool, const std::string& domain) {
  const auto platform = tool.substr(0, tool.find("__"));
  return platform == "intent" || platform == domain;
}
} // namespace

const std::vector<std::string>& FanSpeeds() {
  static const std::vector<std::string> speeds = {"quiet", "low", "medium", "high", "turbo"};
  return speeds;
}

bool WriteOnly(const Device& d, const std::string& parameter) {
  return parameter == "light_colour" || parameter == "light_colour_temp" || parameter == "mute" || IsScenario(d.domain);
}

bool IsScenario(const std::string& domain) {
  return domain == "scene" || domain == "script";
}

std::optional<double> ParseNumber(const std::string& text) {
  char* end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  if (text.empty() || end != text.c_str() + text.size() || !std::isfinite(value)) {
    return std::nullopt;
  }
  return value;
}

const std::map<std::string, std::vector<std::string>>& WriteTools(const std::string& domain) {
  static const std::map<std::string, std::vector<std::string>> vacuum_tools = {
    {"on_off", {"vacuum__HassVacuumStart", "vacuum__HassVacuumReturnToBase"}},
    {"vacuum_cleaner_status", {"vacuum__HassVacuumStart", "vacuum__HassVacuumReturnToBase"}},
  };
  static const std::map<std::string, std::vector<std::string>> tools = {
    {"on_off", {"intent__HassTurnOn", "intent__HassTurnOff"}},
    {"light_brightness", {"light__HassLightSet"}},
    {"light_colour", {"light__HassLightSet"}},
    {"light_colour_temp", {"light__HassLightSet"}},
    {"hvac_temp_set", {"climate__HassClimateSetTemperature"}},
    {"hvac_humidity_set", {"humidifier__HassHumidifierSetpoint"}},
    {"hvac_air_flow_power", {"fan__HassFanSetSpeed"}},
    {"open_percentage", {"intent__HassSetPosition"}},
    {"open_set", {"intent__HassTurnOn", "intent__HassTurnOff", "intent__HassStopMoving"}},
    {"volume_int", {"media_player__HassSetVolume"}},
    {"mute", {"media_player__HassMediaPlayerMute", "media_player__HassMediaPlayerUnmute"}},
  };
  return domain == "vacuum" ? vacuum_tools : tools;
}

std::vector<Device> ParseContext(
  const Json::Value& content, const std::string& server_id, std::vector<std::string>* issues
) {
  auto text = ContextText(content);
  const std::string prefix = "Live Context:";
  if (text.compare(0, prefix.size(), prefix) == 0) {
    const auto newline = text.find('\n');
    if (newline == std::string::npos) {
      return {};
    }
    text.erase(0, newline + 1);
  }
  if (text.empty()) {
    return {};
  }
  const auto rows = YAML::Load(text);
  if (!rows.IsSequence()) {
    throw std::runtime_error("HA context must contain a device sequence");
  }
  std::vector<Device> out;
  for (const auto& row : rows) {
    try {
      if (!row.IsMap()) {
        throw std::runtime_error("Invalid HA device entry");
      }
      Device d;
      d.server_id = server_id;
      d.domain = Scalar(row["domain"]);
      static const std::set<std::string> domains = {
        "light",
        "vacuum",
        "switch",
        "climate",
        "fan",
        "humidifier",
        "sensor",
        "binary_sensor",
        "cover",
        "valve",
        "media_player",
        "scene",
      };
      if (!domains.count(d.domain)) {
        continue;
      }
      d.names = Labels(Scalar(row["names"]));
      d.areas = Labels(Scalar(row["areas"]));
      d.name = d.names.empty() ? "" : d.names.front();
      d.area = d.areas.empty() ? "" : d.areas.front();
      if (d.name.empty()) {
        throw std::runtime_error("HA device has no target name");
      }
      const auto state = Scalar(row["state"]);
      d.online = !state.empty() && state != "unavailable" && state != "unknown";
      const auto attributes = row["attributes"] ? row["attributes"] : YAML::Node(YAML::NodeType::Map);
      if (!attributes.IsMap()) {
        throw std::runtime_error("Invalid HA attributes");
      }
      if (d.domain == "light" || d.domain == "switch") {
        d.category = d.domain == "light" ? "light" : "socket";
        Add(d, "on_off", OnOff(state));
        if (d.domain == "light") {
          auto brightness = Number(attributes["brightness_pct"]);
          if (brightness.isNull()) {
            const auto raw = Number(attributes["brightness"]);
            if (raw.isNumeric() && raw.asDouble() >= 0 && raw.asDouble() <= 255) {
              brightness = raw.asDouble() > 0 ? std::max(1.0, std::round(raw.asDouble() * 100 / 255)) : 0.0;
            }
          }
          if (brightness.isNumeric() && (brightness.asDouble() < 0 || brightness.asDouble() > 100)) {
            brightness = {};
          }
          // For a dimmable light that is off, HA returns an empty brightness: for the model it is 0, so "brighter"
          // turns the light on by one step.
          if (brightness.isNull() && state == "off" && attributes["brightness"].IsDefined()) {
            brightness = 0.0;
          }
          Add(d, "light_brightness", brightness);
          // HA's GetLiveContext reports neither colour nor colour temperature nor supported colour modes, so every
          // light offers both, without a value. A server that reports rgb_color and color_temp_kelvin makes them
          // readable.
          Add(d, "light_colour", Rgb(attributes["rgb_color"]));
          Add(d, "light_colour_temp", ColourTemp(attributes["color_temp_kelvin"]));
        }
      } else if (d.domain == "vacuum") {
        d.category = "vacuum_cleaner";
        static const std::map<std::string, std::string> statuses = {
          {"cleaning", "cleaning"}, {"docked", "docked"}, {"paused", "pause"}, {"returning", "returning_to_dock"}
        };
        const auto status = statuses.find(state);
        Add(
          d, "on_off", status != statuses.end() || state == "idle" ? Json::Value(state == "cleaning") : Json::Value()
        );
        Add(d, "vacuum_cleaner_status", status != statuses.end() ? Json::Value(status->second) : Json::Value());
      } else if (d.domain == "climate") {
        // shortcut: GetLiveContext has no hvac_modes, so every climate is a heater (cooling still
        // works via hvac_temp_set); split out hvac_ac once HA exposes the modes.
        d.category = "hvac_heater";
        Add(d, "on_off", HvacOn(state));
        Add(d, "hvac_temp_set", Number(attributes["temperature"]));
        Add(d, "temperature", Number(attributes["current_temperature"]));
        // HA's humidity of a climate is the target; the measured current_humidity is not in the context.
        if (attributes["humidity"]) {
          Add(d, "hvac_humidity_set", Number(attributes["humidity"]));
        }
      } else if (d.domain == "fan" || d.domain == "humidifier") {
        d.category = d.domain == "fan" ? "hvac_fan" : "hvac_humidifier";
        Add(d, "on_off", OnOff(state));
        // HA reports percentage only for a fan with speeds.
        if (d.domain == "fan" && attributes["percentage"]) {
          Add(d, "hvac_air_flow_power", FanSpeed(Number(attributes["percentage"])));
        }
        if (d.domain == "humidifier" && attributes["humidity"]) {
          Add(d, "hvac_humidity_set", Number(attributes["humidity"]));
        }
      } else if (d.domain == "binary_sensor") {
        const auto sensor = BinarySensors().find(Scalar(attributes["device_class"]));
        if (sensor == BinarySensors().end()) {
          continue;
        }
        d.category = sensor->second.first;
        // on/off is a boolean, as for on_off.
        Add(d, sensor->second.second, OnOff(state));
      } else if (d.domain == "cover" || d.domain == "valve") {
        // HA opens, closes, positions and stops a valve with the same intents as a cover.
        d.category = d.domain == "valve" ? "valve" : CoverCategory(Scalar(attributes["device_class"]));
        // HA reports current_position only for covers with a position: without it the model is not offered a position.
        if (attributes["current_position"]) {
          auto position = Number(attributes["current_position"]);
          if (position.isNumeric() && (position.asDouble() < 0 || position.asDouble() > 100)) {
            position = {};
          }
          Add(d, "open_percentage", position);
        }
        Json::Value open;
        if (state == "open" || state == "closed") {
          open = state == "open" ? "open" : "close";
        }
        Add(d, "open_set", open);
      } else if (d.domain == "media_player") {
        // Only televisions: other players are speakers the model has no category for.
        if (Scalar(attributes["device_class"]) != "tv") {
          continue;
        }
        d.category = "tv";
        Json::Value on;
        if (state == "off" || state == "standby") {
          on = false;
        } else if (
          state == "on" || state == "idle" || state == "playing" || state == "paused" || state == "buffering"
        ) {
          on = true;
        }
        Add(d, "on_off", on);
        auto volume = Number(attributes["volume_level"]);
        if (volume.isNumeric()) {
          volume = volume.asDouble() >= 0 && volume.asDouble() <= 1
            ? std::round(volume.asDouble() * 100)
            : Json::Value();
        }
        Add(d, "volume_int", volume);
        // GetLiveContext has no is_volume_muted: write-only.
        Add(d, "mute", {});
      } else if (d.domain == "scene") {
        // The state of a scene is the time it was last activated, unknown before the first time.
        d.online = state != "unavailable";
        d.category = kScenarioCategory;
        Add(d, "on_off", {});
      } else {
        const auto parameter = SensorParameter(Scalar(attributes["device_class"]));
        if (parameter.empty()) {
          continue;
        }
        d.category = "sensor_temp";
        Add(d, parameter, Number(row["state"]));
      }
      // Unknown is a state HA has not got yet; only unavailable means HA cannot reach the device.
      d.features["online"] = {false, state != "unavailable"};
      out.push_back(std::move(d));
    } catch (const std::exception& e) {
      // Catalog: one odd entity must not hide the whole house. Reads stay strict.
      if (!issues) {
        throw;
      }
      issues->push_back(server_id + ": " + e.what());
    }
  }
  return out;
}

CatalogPtr CatalogBuilder::Build(const McpCatalogSnapshot& snapshot, const CatalogParams& params) {
  auto out = std::make_shared<Catalog>();
  out->status = snapshot.status;
  out->error = snapshot.error;
  out->revision = snapshot.revision;
  if (out->status == AssistantStatus::ERROR) {
    return out;
  }
  for (const auto& tool : snapshot.tools) {
    out->tools[tool.server_id].insert(tool.tool_name);
  }
  std::map<std::string, size_t> resources;
  for (const auto& state : snapshot.latest_resource_states) {
    if (state.uri == params.resource_uri) {
      ++resources[state.server_id];
    }
  }
  for (const auto& state : snapshot.latest_resource_states) {
    if (state.uri != params.resource_uri) {
      continue;
    }
    if (
      !params.server_ids.empty()
      && std::find(params.server_ids.begin(), params.server_ids.end(), state.server_id) == params.server_ids.end()
    ) {
      continue;
    }
    try {
      if (resources[state.server_id] != 1) {
        throw std::runtime_error("Duplicate HA context resource");
      }
      if (!params.max_context_bytes || state.data_json.size() > params.max_context_bytes) {
        throw std::runtime_error("HA context exceeds size limit");
      }
      if (!HasTool(*out, state.server_id, "homeassistant__GetLiveContext")) {
        throw std::runtime_error("HA server lacks GetLiveContext");
      }
      auto devices = ParseContext(ParseJsonOrThrow(state.data_json), state.server_id, &out->issues);
      for (const auto& tool : snapshot.tools) {
        if (auto script = tool.server_id == state.server_id ? ScriptDevice(tool) : std::nullopt) {
          devices.push_back(std::move(*script));
        }
      }
      // HA targets names and aliases case-insensitively; an omitted area matches every area.
      // A device is addressable only if its own name and area select it alone.
      // Kept apart from the devices: an accepted device is moved into the catalog while the later ones are checked.
      struct LabelKeys {
        std::string domain;
        std::u16string name, area;
        std::set<std::u16string> names, areas;
      };
      std::vector<LabelKeys> labels;
      labels.reserve(devices.size());
      for (const auto& d : devices) {
        labels.push_back({d.domain, Key(d.name), Key(d.area), Keys(d.names), Keys(d.areas)});
      }
      for (size_t i = 0; i < devices.size(); ++i) {
        auto& d = devices[i];
        size_t matches = 0, house_matches = 0;
        for (size_t j = 0; j < devices.size(); ++j) {
          const bool named = labels[j].domain == labels[i].domain && labels[j].names.count(labels[i].name);
          matches += named && (d.area.empty() || labels[j].areas.count(labels[i].area));
          house_matches += named;
        }
        const auto identity = Identity(d);
        if (matches != 1) {
          out->issues.push_back("Ambiguous HA target: " + d.name);
          continue;
        }
        const auto& write_tools = WriteTools(d.domain);
        // A device whose only parameter is online gives the model nothing to do.
        auto online = d.features.extract("online");
        for (auto it = d.features.begin(); it != d.features.end();) {
          const auto tools = write_tools.find(it->first);
          // A script is run by its own tool; everything else by the tools of its parameter.
          // HassHumidifierSetpoint has no area: the name alone must select the humidifier in the whole house, or HA
          // refuses it as a duplicate name.
          it->second.writable = !d.tool.empty()
            || (tools != write_tools.end() && std::all_of(tools->second.begin(), tools->second.end(), [&](const auto& tool) {
                  return HasTool(*out, d.server_id, tool) && ToolFits(tool, d.domain)
                    && (tool != "humidifier__HassHumidifierSetpoint" || house_matches == 1);
                }));
          // A write-only parameter without its tool has nothing to offer the model.
          it = !it->second.writable && WriteOnly(d, it->first) ? d.features.erase(it) : std::next(it);
        }
        if (d.features.empty()) {
          continue;
        }
        if (params.include_online) {
          d.features.insert(std::move(online));
        }
        const auto [it, inserted] = aliases_.try_emplace(identity);
        if (inserted) {
          it->second = d.category + "_" + std::to_string(next_index_[d.category]++);
        }
        out->devices.emplace(it->second, std::move(d));
      }
    } catch (const std::exception& e) {
      out->issues.push_back(state.server_id + ": " + e.what());
    }
  }
  if (out->devices.empty()) {
    out->status = AssistantStatus::ERROR;
    out->error = out->issues.empty() ? "HA context has no supported devices" : out->issues.front();
  }
  return out;
}

SceneSnapshot RenderScene(const Catalog& catalog, const SceneParams& params) {
  SceneSnapshot result;
  result.status = catalog.status;
  result.error = catalog.error;
  if (catalog.status != AssistantStatus::OK) {
    return result;
  }
  std::vector<std::pair<std::string, const Device*>> devices;
  devices.reserve(catalog.devices.size());
  for (const auto& [alias, device] : catalog.devices) {
    devices.emplace_back(alias, &device);
  }
  const auto index = [](const auto& item) {
    return item.first.substr(item.second->category.size() + 1);
  };
  std::sort(devices.begin(), devices.end(), [&](const auto& a, const auto& b) {
    const auto& x = a.second->category;
    const auto& y = b.second->category;
    return x != y ? x < y : index(a) < index(b);
  });
  for (const auto& [alias, pointer] : devices) {
    const auto& d = *pointer;
    const bool prefixed = IsScenario(d.domain) && !params.scenario_prefix.empty();
    std::string line = "<d_" + d.category + ">" + alias.substr(d.category.size() + 1) + " "
      + Label(prefixed ? params.scenario_prefix + " " + d.name : d.name);
    if (!d.area.empty()) {
      line += " [" + Label(d.area) + "]";
    }
    for (const auto& [name, feature] : d.features) {
      line += "<" + std::string(feature.writable ? "p_rw_" : "p_ro_") + name + ">";
    }
    line += '\n';
    if (result.text.size() + line.size() > params.max_chars) {
      result.text.clear();
      result.status = AssistantStatus::ERROR;
      result.error = "HA scene exceeds model context budget";
      return result;
    }
    result.text += line;
  }
  result.version = "ha-scene#" + std::to_string(std::hash<std::string>{}(result.text));
  return result;
}
} // namespace giga_edge::ha
