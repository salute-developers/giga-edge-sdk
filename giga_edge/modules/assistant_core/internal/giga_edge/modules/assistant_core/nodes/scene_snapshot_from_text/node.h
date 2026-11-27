#pragma once

#include "giga_edge/lib/config_parser/config_parser_fwd.h"
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"

#include <string>

namespace giga_edge {

struct SceneSnapshotFromTextParams {
  std::string scene_version = "scene-v1";
  bool allow_empty = true;
  bool version_includes_scene_text_hash = true;
};

template<>
SceneSnapshotFromTextParams ParseConfig(const YAML::Node& config);

class SceneSnapshotFromText : public NodeBase {
public:
  SceneSnapshotFromText(const NodeBaseConfig& base_config, const SceneSnapshotFromTextParams& params);

  void ProcessOnce() override;
  void Reset() override {}

private:
  REGISTER_INPUT(scene_snapshot_from_text, text, Text, std::string, "Scene description as plain text.");
  REGISTER_OUTPUT(
    scene_snapshot_from_text,
    scene_snapshot,
    SceneSnapshot,
    SceneSnapshot,
    "SceneSnapshot with a version derived from the text."
  );

  SceneSnapshotFromTextParams params_;
};

REGISTER_NODE(
  scene_snapshot_from_text, SceneSnapshotFromText, SceneSnapshotFromTextParams, "Wraps scene text into a SceneSnapshot."
);

} // namespace giga_edge
