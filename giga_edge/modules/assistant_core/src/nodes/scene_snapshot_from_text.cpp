#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/modules/assistant_core/nodes/scene_snapshot_from_text/node.h"

#include <functional>
#include <sstream>

namespace giga_edge {

template<>
SceneSnapshotFromTextParams ParseConfig(const YAML::Node& config) {
  SceneSnapshotFromTextParams result;
  result.scene_version = config["scene_version"].as<std::string>(result.scene_version);
  result.allow_empty = config["allow_empty"].as<bool>(result.allow_empty);
  result.version_includes_scene_text_hash = config["version_includes_scene_text_hash"].as<bool>(
    result.version_includes_scene_text_hash
  );
  return result;
}

SceneSnapshotFromText::SceneSnapshotFromText(
  const NodeBaseConfig& base_config, const SceneSnapshotFromTextParams& params
)
  : NodeBase(base_config)
  , params_(params) {}

void SceneSnapshotFromText::ProcessOnce() {
  const std::string& text = GetText();
  std::string version = params_.scene_version;
  if (params_.version_includes_scene_text_hash) {
    std::ostringstream oss;
    oss << version << '#' << std::hex << std::hash<std::string>{}(text);
    version = oss.str();
  }
  PushSceneSnapshot(SceneSnapshot{.text = text, .version = version, .allow_empty = params_.allow_empty});
}

} // namespace giga_edge
