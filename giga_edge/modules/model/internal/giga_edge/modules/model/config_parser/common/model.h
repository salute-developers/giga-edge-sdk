#pragma once

#include "giga_edge/modules/model/models/crnn/params.h"

namespace giga_edge {

ModelType ParseModelType(const std::string& model_type);

} // namespace giga_edge