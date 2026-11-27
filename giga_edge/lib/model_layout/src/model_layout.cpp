#include "giga_edge/lib/model_layout/model_layout.h"

#include "giga_edge/lib/data_reader/file_path.h"

#include <json/json.h>

#include <fstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace giga_edge {
namespace {

Json::Value ReadJson(const std::filesystem::path& path) {
  std::ifstream file(path);
  if (!file) {
    throw std::runtime_error("no metadata: " + Utf8Path(path));
  }
  if (std::filesystem::file_size(path) > 65536) {
    throw std::runtime_error("model metadata is too large: " + Utf8Path(path));
  }
  Json::CharReaderBuilder builder;
  builder["rejectDupKeys"] = true;
  builder["failIfExtra"] = true;
  std::string errors;
  Json::Value root;
  if (!Json::parseFromStream(builder, file, &root, &errors) || !root.isObject()) {
    throw std::runtime_error("bad " + Utf8Path(path) + ": " + errors);
  }
  return root;
}

std::unordered_map<std::string, SubmodelVersion> ReadVersions(
  const Json::Value& versions, const std::filesystem::path& path
) {
  if (!versions.isObject()) {
    throw std::runtime_error("bad " + Utf8Path(path) + ": 'versions' is not an object");
  }
  std::unordered_map<std::string, SubmodelVersion> result;
  for (const auto& version : versions.getMemberNames()) {
    if (!versions[version].isString()) {
      throw std::runtime_error("bad " + Utf8Path(path) + ": version '" + version + "' is not a config path");
    }
    result.emplace(version, SubmodelVersion{versions[version].asString()});
  }
  if (result.empty()) {
    throw std::runtime_error("bad " + Utf8Path(path) + ": no versions");
  }
  return result;
}

std::string PartData(const std::filesystem::path& dir) {
  if (const auto directory = dir / "model"; std::filesystem::is_directory(directory)) {
    return Utf8Path(directory);
  }
  return Utf8Path(dir / "model.tar");
}

} // namespace

RuntimePart ReadPart(const std::filesystem::path& dir, const std::string& id) {
  if (!std::filesystem::is_directory(dir)) {
    throw std::runtime_error("no part directory: " + Utf8Path(dir));
  }
  const auto meta = dir / "meta.json";
  const auto root = ReadJson(meta);
  // Not inside the aggregate: GCC < 13 leaks already built members when a later initializer throws.
  auto versions = ReadVersions(root["versions"], meta);
  auto data = PartData(dir);
  return RuntimePart{
    .id = id, .data = ModelFileData{std::move(data)}, .versions = std::move(versions), .deferred = true
  };
}

ModelLayout ReadModelLayout(const std::filesystem::path& model_root) {
  if (!std::filesystem::is_directory(model_root)) {
    throw std::runtime_error("no model directory: " + Utf8Path(model_root));
  }
  const auto meta = model_root / "meta.json";
  const auto root = ReadJson(meta);

  ModelLayout layout;
  const auto core_dir = model_root / FilePath(root.get("core", "core").asString());
  layout.core = RuntimePart{
    .id = "core",
    .data = ModelFileData{Utf8Path(core_dir)},
    .versions = {{CORE_VERSION_NAME, {"config.yaml"}}},
  };
  const auto& parts = root["parts"];
  if (!parts.isObject()) {
    throw std::runtime_error("bad " + Utf8Path(meta) + ": 'parts' is not an object");
  }
  // Optional parts may be missing until downloaded.
  std::unordered_set<std::string> optional;
  if (root.isMember("optional_parts")) {
    const auto& ids = root["optional_parts"];
    if (!ids.isArray()) {
      throw std::runtime_error("bad " + Utf8Path(meta) + ": 'optional_parts' is not an array");
    }
    for (const auto& id : ids) {
      if (!id.isString() || !parts.isMember(id.asString())) {
        throw std::runtime_error("bad " + Utf8Path(meta) + ": optional part is not one of 'parts'");
      }
      optional.insert(id.asString());
    }
  }
  for (const auto& id : parts.getMemberNames()) {
    if (!parts[id].isString()) {
      throw std::runtime_error("bad " + Utf8Path(meta) + ": part '" + id + "' is not a path");
    }
    const auto dir = model_root / FilePath(parts[id].asString());
    if (optional.count(id) != 0 && !std::filesystem::exists(dir)) {
      continue;
    }
    layout.parts.push_back(ReadPart(dir, id));
  }
  return layout;
}

} // namespace giga_edge
