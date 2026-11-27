#pragma once

namespace YAML {
class Node;
} // namespace YAML

namespace giga_edge {

template<class T>
T ParseConfig(const YAML::Node& config);

} // namespace giga_edge
