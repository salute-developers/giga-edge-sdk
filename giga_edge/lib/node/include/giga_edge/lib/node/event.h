#pragma once

#include "giga_edge/lib/submodel_version/submodel_version.h"

#include <any>
#include <string>

namespace giga_edge {

struct Event {
  std::string node_name;

  std::any event;

  SubmodelVersionChain submodel_version_chain;
};

} // namespace giga_edge
