#include "bug_report.h"

#include "giga_edge/lib/common/str_utils.h"

#include <json/json.h>

#include <any>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace giga_edge {
namespace {

const char* const kSceneOutput = "scene_snapshot";
constexpr int kSchemaVersion = 1;

// The active versions: a key of CollectConsumedVersions is the chain without its last version (id#version#...#id).
std::vector<SubmodelVersionChain> ActiveChains(const Runtime& runtime) {
  std::vector<SubmodelVersionChain> chains;
  for (const auto& [slot, version] : runtime.CollectConsumedVersions()) {
    const std::string path = slot + "#" + version;
    const auto ids = SplitString(path, '#');
    SubmodelVersionChain chain;
    for (size_t i = 0; i + 1 < ids.size(); i += 2) {
      chain.ids_chain.push_back({.id = std::string(ids[i]), .version = std::string(ids[i + 1])});
    }
    chains.push_back(std::move(chain));
  }
  return chains;
}

} // namespace

void BugReports::Watch(RuntimeManager& manager) {
  const auto runtime = manager.Model();
  core_version_ = runtime->Version();
  const auto chains = ActiveChains(*runtime);
  if (const auto metas = manager.CollectSubmodelsMeta(chains, SubmodelRequireMode::NONE)) {
    for (const auto& meta : *metas) {
      versions_[meta.version_id] = meta.config_version.value_or("");
    }
  }
  for (const auto& chain : chains) {
    if (runtime->HasOutput(chain, kSceneOutput)) {
      runtime->Subscribe<SceneSnapshot>(chain, kSceneOutput, [this](const SceneSnapshot& scene, const std::any&) {
        std::lock_guard lock(mutex_);
        scene_ = scene;
      });
      has_scene_ = true;
      return;
    }
  }
}

std::string BugReports::Write(const std::string& comment) {
  if (!has_scene_) {
    throw std::runtime_error("no scene: the model runs without function calling");
  }
  std::optional<SceneSnapshot> scene;
  {
    std::lock_guard lock(mutex_);
    scene = scene_;
  }
  if (!scene) {
    throw std::runtime_error("no scene yet");
  }

  const auto now = std::chrono::system_clock::now();
  const auto seconds = std::chrono::system_clock::to_time_t(now);
  std::tm local{};
  localtime_r(&seconds, &local);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
  std::ostringstream time;
  time << std::put_time(&local, "%FT%T%z");
  std::ostringstream name;
  name << std::put_time(&local, "%Y%m%d_%H%M%S_") << std::setw(3) << std::setfill('0') << ms << ".json";

  Json::Value report;
  report["schema_version"] = kSchemaVersion;
  report["time"] = time.str();
  report["comment"] = comment;
  report["scene"]["text"] = scene->text;
  report["scene"]["version"] = scene->version;
  report["scene"]["status"] = ToString(scene->status);
  report["scene"]["error"] = scene->error;
  report["model"]["core"] = core_version_;
  report["model"]["versions"] = Json::objectValue;
  for (const auto& [id, version] : versions_) {
    report["model"]["versions"][id] = version;
  }

  std::error_code error;
  std::filesystem::create_directories(dir_, error);
  if (error) {
    throw std::runtime_error("cannot create " + dir_ + ": " + error.message());
  }
  const auto path = std::filesystem::absolute(std::filesystem::path(dir_) / name.str()).string();
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "  ";
  writer["emitUTF8"] = true;
  std::ofstream out(path);
  out << Json::writeString(writer, report) << '\n';
  if (!out) {
    throw std::runtime_error("cannot write " + path);
  }
  return path;
}

} // namespace giga_edge
