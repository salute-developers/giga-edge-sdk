#include "giga_edge/lib/config_parser/config_parser.h"

#include "giga_edge/lib/submodel_version/submodel_version.h"

namespace giga_edge {

template<>
SubmodelVersionId ParseConfig(const YAML::Node& config) {
  SubmodelVersionId result;
  PARSE_OPTIONAL_FIELD(id, "");
  PARSE_OPTIONAL_FIELD(version, "");
  return result;
}

} // namespace giga_edge
