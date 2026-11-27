#pragma once

#include "giga_edge/lib/runtime_manager/runtime_manager.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace giga_edge {

// Problem reports of the demo: the scene of the house as the model sees it (the output scene_snapshot of function
// calling) and the comment of the user, one JSON file per report.
class BugReports {
public:
  explicit BugReports(std::string dir)
    : dir_(std::move(dir)) {}

  // Follows the scene of the active version that has one (a stub of function calling has none) and takes the versions
  // of the model. Call before the graph ticks.
  void Watch(RuntimeManager& manager);
  // Returns the path of the written file; throws with the reason when there is no scene or the file is not written.
  std::string Write(const std::string& comment);

private:
  std::string dir_;
  std::string core_version_;
  std::map<std::string, std::string> versions_;
  bool has_scene_ = false;
  std::mutex mutex_;
  std::optional<SceneSnapshot> scene_;
};

} // namespace giga_edge
