#pragma once

#include "giga_edge/lib/runtime_data/runtime_data.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace giga_edge {

struct ModelLayout {
  RuntimePart core;
  std::vector<RuntimePart> parts;
};

// A part directory holds meta.json ({"versions": {"<version>": "<config path>"}}) and its data: the directory model/
// or, if it is absent, model.tar. The part is loaded on first use (deferred).
RuntimePart ReadPart(const std::filesystem::path& dir, const std::string& id);

// The model directory holds meta.json: {"core": "<dir, default core>", "parts": {"<id>": "<dir>"},
// "optional_parts": ["<id>"]}; paths are relative to the model directory, an optional part may be missing.
ModelLayout ReadModelLayout(const std::filesystem::path& model_root);

} // namespace giga_edge
