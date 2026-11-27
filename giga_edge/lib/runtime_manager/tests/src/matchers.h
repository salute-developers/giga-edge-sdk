#pragma once

#include "giga_edge/lib/runtime_data/runtime_data.h"

#include <unordered_set>

namespace giga_edge {

inline bool operator==(const UpdateAction& lhs, const UpdateAction& rhs) {
  // Specify available_features in the expected action when the feature set
  // itself is under test.
  const bool check_available_features = !rhs.available_features.empty();
  const bool check_inactive_versions = !rhs.inactive_versions.empty();
  return lhs.freed_parts == rhs.freed_parts && lhs.required_versions == rhs.required_versions
    && (!check_inactive_versions || lhs.inactive_versions == rhs.inactive_versions)
    && lhs.consumed_versions == rhs.consumed_versions
    && (!check_available_features || lhs.available_features == rhs.available_features);
}

} // namespace giga_edge
