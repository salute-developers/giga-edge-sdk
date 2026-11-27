#include "giga_edge/modules/model/config_parser/common/model.h"

#include <stdexcept>
#include <unordered_map>

namespace giga_edge {

ModelType ParseModelType(const std::string& model_type) {
  static std::unordered_map<std::string, ModelType> const table = {
    {"stateful_crnn", ModelType::STATEFUL_CRNN},
    {"window_crnn", ModelType::WINDOW_CRNN},
  };

  auto it = table.find(model_type);
  if (it != table.end()) {
    return it->second;
  } else {
    throw std::runtime_error("Unknown model type " + model_type);
  }
}

} // namespace giga_edge