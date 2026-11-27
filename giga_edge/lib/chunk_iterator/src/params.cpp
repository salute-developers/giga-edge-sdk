#include "giga_edge/lib/chunk_iterator/chunk_iterator.h"
#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
ChunkIteratorParams ParseConfig(const YAML::Node& config) {
  ChunkIteratorParams result;
  PARSE_REQUIRED_FIELD(chunk_size);
  PARSE_OPTIONAL_FIELD(step_size, result.chunk_size);
  return result;
}
} // namespace giga_edge
