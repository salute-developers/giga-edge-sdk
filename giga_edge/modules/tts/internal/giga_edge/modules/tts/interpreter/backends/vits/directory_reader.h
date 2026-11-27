#pragma once

#include "giga_edge/lib/data_reader/data_reader.h"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>

namespace giga_edge::vits {

// A directory of another reader as the root: the models of a part read with it find their external weights
// (<name>.onnx.data, named relative to the model file as onnx saves them), since ONNX Runtime looks for them under the
// path prefix of the reader the model is loaded with.
class DirectoryReader : public IDataReader {
public:
  // `directory` is relative to the root of `parent`, with or without a final '/'; empty is the root itself.
  DirectoryReader(IDataReaderPtr parent, std::string directory)
    : parent_(std::move(parent))
    , directory_(std::move(directory)) {
    if (!directory_.empty() && directory_.back() != '/') {
      directory_ += '/';
    }
  }

  IMovableDataSpanPtr ReadTransientData(const std::string& file_path) override {
    return parent_->ReadTransientData(directory_ + file_path);
  }

  IDataSpanPtr ReadPersistentData(const std::string& file_path) override {
    return parent_->ReadPersistentData(directory_ + file_path);
  }

  std::optional<std::string> GetPathPrefix() const override {
    auto prefix = parent_->GetPathPrefix();
    if (prefix && !directory_.empty()) {
      prefix = (std::filesystem::path(*prefix) / directory_).string();
    }
    return prefix;
  }

private:
  IDataReaderPtr parent_;
  std::string directory_; // empty or ends with '/'
};

} // namespace giga_edge::vits
