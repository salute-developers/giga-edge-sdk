#pragma once
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/smart_home_ha/catalog.h"
namespace giga_edge {
template<>
ha::SceneParams ParseConfig(const YAML::Node& config);
class HaSceneNode : public NodeBase {
public:
  HaSceneNode(const NodeBaseConfig& base, const ha::SceneParams& params)
    : NodeBase(base)
    , params_(params) {}
  void ProcessOnce() override;
  void Reset() override {
    catalog_.reset();
  }
private:
  REGISTER_INPUT(ha_scene, ha_catalog, Catalog, ha::CatalogPtr, "Normalized Home Assistant targets.");
  REGISTER_OUTPUT(ha_scene, scene_snapshot, Scene, SceneSnapshot, "Compact device scene for the trained FC model.");
  ha::SceneParams params_;
  ha::CatalogPtr catalog_;
  SceneSnapshot scene_;
};
REGISTER_NODE(ha_scene, HaSceneNode, ha::SceneParams, "Renders HA targets in the trained compact scene format.");
} // namespace giga_edge
