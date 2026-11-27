#pragma once

#include "giga_edge/lib/data_reader/options.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace giga_edge {

struct ModelFileData {
  std::string path;
  DataReaderOptions reader_options;
};

class ITemporaryDataHolder {
public:
  virtual std::pair<const char*, size_t> Data() const = 0;
  virtual ~ITemporaryDataHolder() = default;
};

using ITemporaryDataHolderPtr = std::unique_ptr<ITemporaryDataHolder>;

struct ModelTemporaryTarData {
  std::function<ITemporaryDataHolderPtr()> data_provider;
  DataReaderOptions reader_options;
};

struct ModelPersistentTarData {
  const char* data;
  size_t size;
  DataReaderOptions reader_options;
};

using ModelData = std::variant<ModelFileData, ModelPersistentTarData, ModelTemporaryTarData>;

static constexpr auto CORE_VERSION_NAME{"core"};

using AvailableFeatures = std::unordered_set<std::string>;

struct SubmodelVersion {
  std::string config_path;
};

struct RuntimePart {
  std::string id;
  ModelData data;
  std::unordered_map<std::string, SubmodelVersion> versions;
  bool permanent = false; // a part that is kept even when unused, for example one shipped with the firmware
  bool deferred = false;
};

struct UpdateAction {
  std::unordered_set<std::string> freed_parts;
  std::unordered_set<std::string> required_versions;
  std::unordered_set<std::string> inactive_versions;
  std::unordered_map<std::string, std::string> consumed_versions;
  AvailableFeatures available_features;
};

} // namespace giga_edge
