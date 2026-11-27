#pragma once

#include "giga_edge/lib/submodel_version/submodel_version.h"

namespace giga_edge {

struct ResetEvent {};

struct SetActiveSubmodelEvent {
  SubmodelVersionChain submodel_version_chain;
};

} // namespace giga_edge