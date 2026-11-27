#pragma once

#include <string>
#include <vector>

namespace giga_edge {

struct SubmodelVersionId {
  std::string id;
  std::string version;
};

bool operator==(const SubmodelVersionId& l, const SubmodelVersionId& r);

struct SubmodelVersionChain {
  std::vector<SubmodelVersionId> ids_chain;

  std::string AsString() const;

  SubmodelVersionChain PopFront() const;
  SubmodelVersionChain PopBack() const;

  bool IsPrefixOf(const SubmodelVersionChain& other) const;
};

bool operator==(const SubmodelVersionChain& l, const SubmodelVersionChain& r);

} // namespace giga_edge

template<>
struct std::hash<giga_edge::SubmodelVersionChain> {
  std::size_t operator()(const giga_edge::SubmodelVersionChain& version) const noexcept;
};