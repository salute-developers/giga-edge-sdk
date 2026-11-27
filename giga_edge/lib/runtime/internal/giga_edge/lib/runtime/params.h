#pragma once

#include "giga_edge/lib/environment/environment.h"
#include "giga_edge/lib/node/node_params.h"
#include "giga_edge/lib/runtime/submodel.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace giga_edge {

struct SubmodelParams {
  std::string version;
  std::string lib_version;
  EnvironmentsMapParams environments_map_params;

  std::vector<NodeParams> nodes;
  std::vector<SubmodelSignature> submodels;

  std::unordered_map<std::string, DefaultSubmodel> default_submodels;
  std::vector<SubmodelFeature> features;
};

struct RuntimeParams : public SubmodelParams {
  int num_threads;
  // How many submodels of one level load at once (1: one by one); more than 1 needs the onnx environment in the core.
  int load_threads = 1;
  bool realtime_priority;
  int num_realtime_threads;
  std::string thread_name_prefix;
  CommonNodeParams common_node_params;

  int statistics_every_batches;
};

} // namespace giga_edge
