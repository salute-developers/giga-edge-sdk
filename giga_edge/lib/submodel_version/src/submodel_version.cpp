#include "giga_edge/lib/submodel_version/submodel_version.h"

#include "giga_edge/lib/common/str_utils.h"

std::size_t std::hash<giga_edge::SubmodelVersionChain>::operator()(
  const giga_edge::SubmodelVersionChain& version
) const noexcept {
  return std::hash<std::string>{}(version.AsString());
}

namespace giga_edge {

bool operator==(const SubmodelVersionId& l, const SubmodelVersionId& r) {
  return l.id == r.id && l.version == r.version;
}

std::string SubmodelVersionChain::AsString() const {
  return JoinStrings(ids_chain, "#", [](const auto& version) {
    return version.id + (version.version.empty() ? "" : "#" + version.version);
  });
}

bool SubmodelVersionChain::IsPrefixOf(const SubmodelVersionChain& other) const {
  if (ids_chain.empty()) {
    return true;
  }

  if (ids_chain.size() > other.ids_chain.size()) {
    return false;
  }

  for (int i = 0; i < ids_chain.size(); ++i) {
    if (ids_chain[i].id != other.ids_chain[i].id) {
      return false;
    }

    if (i + 1 < ids_chain.size() && ids_chain[i].version != other.ids_chain[i].version) {
      return false;
    }

    if (i + 1 == ids_chain.size()) {
      return ids_chain[i].version.empty() || ids_chain[i].version == other.ids_chain[i].version;
    }
  }

  throw std::runtime_error("Unreachable");
}

SubmodelVersionChain SubmodelVersionChain::PopFront() const {
  return SubmodelVersionChain{
    .ids_chain = {ids_chain.begin() + 1, ids_chain.end()},
  };
}

SubmodelVersionChain SubmodelVersionChain::PopBack() const {
  return SubmodelVersionChain{
    .ids_chain = {ids_chain.begin(), ids_chain.end() - 1},
  };
}

bool operator==(const SubmodelVersionChain& l, const SubmodelVersionChain& r) {
  return l.ids_chain == r.ids_chain;
}

} // namespace giga_edge
