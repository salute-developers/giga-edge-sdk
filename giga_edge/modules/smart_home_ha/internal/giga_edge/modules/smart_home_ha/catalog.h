#pragma once
#include "giga_edge/modules/assistant_core/contracts/types.h"
#include "giga_edge/modules/mcp/contracts/types.h"

#include <json/json.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace giga_edge::ha {
struct Feature {
  bool writable = false;
  Json::Value value;
};
struct Device {
  std::string server_id, domain, name, area, category;
  // A script is not an entity of the context but a tool of its own: the one that runs it.
  std::string tool;
  std::vector<std::string> names, areas;
  bool online = true;
  std::map<std::string, Feature> features;
};
struct Catalog {
  std::map<std::string, Device> devices;
  std::map<std::string, std::set<std::string>> tools;
  std::vector<std::string> issues;
  std::string revision, error;
  AssistantStatus status = AssistantStatus::OK;
};
using CatalogPtr = std::shared_ptr<const Catalog>;
struct CatalogParams {
  std::string environment_name = "mcp";
  uint32_t request_timeout_ms = 15000;
  std::string resource_uri = "homeassistant://assist/context-snapshot";
  std::vector<std::string> server_ids;
  size_t max_context_bytes = 262144;
  // HA does not notify about new or renamed entities over HTTP; 0 disables polling.
  uint32_t refresh_interval_ms = 60000;
  // A context listed by the server but not read yet: a tick with an empty house waits for it at most this long (0: no
  // wait), the read goes on after; an unread or failed context is read again every context_retry_ms.
  uint32_t context_wait_ms = 1500;
  uint32_t context_retry_ms = 5000;
  // Room of the assistant (an HA area name): Assist knows no speaker location, so it comes from the config.
  std::string current_room;
  // Every device gets the read-only parameter online: false while HA reports the entity unavailable.
  bool include_online = false;
};
struct SceneParams {
  size_t max_chars = 18000;
  // The word the model sees before the name of a scene or a script ("<prefix> <name>"): the one people say when
  // they ask for it. Empty: the name alone.
  std::string scenario_prefix;
};
class CatalogBuilder {
public:
  CatalogPtr Build(const McpCatalogSnapshot& snapshot, const CatalogParams& params = {});
private:
  std::map<std::string, std::string> aliases_;
  std::map<std::string, size_t> next_index_;
};
// Also consumes the JSON envelope returned by GetLiveContext. With issues, malformed entries
// are skipped and reported there; without, any malformed entry throws.
std::vector<Device> ParseContext(
  const Json::Value& content, const std::string& server_id, std::vector<std::string>* issues = nullptr
);
// A scene or a script of HA: HA reports no on/off state of it, so its on_off has no value.
bool IsScenario(const std::string& domain);
// HA never reports a value for these, so they exist only while their write tool does. Colour is still read from a
// server that reports it.
bool WriteOnly(const Device& device, const std::string& parameter);
// The whole text is a finite number (strtod syntax).
std::optional<double> ParseNumber(const std::string& text);
// The speeds of hvac_air_flow_power, slowest first: HA sets a fan by percentage, and its ordered list helpers map
// these names to it. auto is a preset mode in HA, which MCP cannot set.
const std::vector<std::string>& FanSpeeds();
// MCP tools a parameter needs to be writable; Build marks features writable from it.
const std::map<std::string, std::vector<std::string>>& WriteTools(const std::string& domain);
SceneSnapshot RenderScene(const Catalog& catalog, const SceneParams& params = {});
} // namespace giga_edge::ha
