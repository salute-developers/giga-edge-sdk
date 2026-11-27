#pragma once

#include "giga_edge/modules/mcp/types.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace giga_edge {
namespace mcp {
namespace detail {

template<class T>
std::shared_ptr<const std::vector<T>> MakeSharedSnapshot(std::vector<T> items) {
  return std::make_shared<const std::vector<T>>(std::move(items));
}

inline bool MatchesTool(const McpToolDescriptor& lhs, const McpToolDescriptor& rhs) {
  return lhs.server_id == rhs.server_id && lhs.tool_name == rhs.tool_name && lhs.title == rhs.title
    && lhs.description == rhs.description && lhs.input_schema == rhs.input_schema;
}

inline bool MatchesResource(const McpResourceDescriptor& lhs, const McpResourceDescriptor& rhs) {
  return lhs.server_id == rhs.server_id && lhs.uri == rhs.uri && lhs.name == rhs.name && lhs.mime_type == rhs.mime_type
    && lhs.description == rhs.description && lhs.subscribable == rhs.subscribable;
}

inline bool MatchesResourceTemplate(
  const McpResourceTemplateDescriptor& lhs, const McpResourceTemplateDescriptor& rhs
) {
  return lhs.server_id == rhs.server_id && lhs.uri_template == rhs.uri_template && lhs.name == rhs.name
    && lhs.mime_type == rhs.mime_type && lhs.description == rhs.description;
}

inline bool MatchesLatestResourceState(const McpLatestResourceState& lhs, const McpLatestResourceState& rhs) {
  return lhs.server_id == rhs.server_id && lhs.uri == rhs.uri && lhs.data == rhs.data;
}

inline bool MatchesResourceSubscriptionState(
  const McpResourceSubscriptionState& lhs, const McpResourceSubscriptionState& rhs
) {
  return lhs.server_id == rhs.server_id && lhs.uri == rhs.uri && lhs.status == rhs.status
    && lhs.error_message == rhs.error_message;
}

template<class T, class MatchFn>
bool VectorsEqual(const std::vector<T>& lhs, const std::vector<T>& rhs, MatchFn match) {
  if (lhs.size() != rhs.size()) {
    return false;
  }

  for (size_t i = 0; i < lhs.size(); ++i) {
    if (!match(lhs[i], rhs[i])) {
      return false;
    }
  }
  return true;
}

inline void SortTools(std::vector<McpToolDescriptor>& tools) {
  std::sort(tools.begin(), tools.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.server_id != rhs.server_id) {
      return lhs.server_id < rhs.server_id;
    }
    return lhs.tool_name < rhs.tool_name;
  });
}

inline void SortResources(std::vector<McpResourceDescriptor>& resources) {
  std::sort(resources.begin(), resources.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.server_id != rhs.server_id) {
      return lhs.server_id < rhs.server_id;
    }
    return lhs.uri < rhs.uri;
  });
}

inline void SortResourceTemplates(std::vector<McpResourceTemplateDescriptor>& templates) {
  std::sort(templates.begin(), templates.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.server_id != rhs.server_id) {
      return lhs.server_id < rhs.server_id;
    }
    return lhs.uri_template < rhs.uri_template;
  });
}

inline void SortLatestResourceStates(std::vector<McpLatestResourceState>& states) {
  std::sort(states.begin(), states.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.server_id != rhs.server_id) {
      return lhs.server_id < rhs.server_id;
    }
    return lhs.uri < rhs.uri;
  });
}

inline void SortResourceSubscriptionStates(std::vector<McpResourceSubscriptionState>& states) {
  std::sort(states.begin(), states.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.server_id != rhs.server_id) {
      return lhs.server_id < rhs.server_id;
    }
    return lhs.uri < rhs.uri;
  });
}

template<class T, class Predicate>
std::vector<T> FilterItems(const std::vector<T>& items, Predicate predicate) {
  std::vector<T> filtered;
  filtered.reserve(items.size());
  for (const auto& item : items) {
    if (predicate(item)) {
      filtered.push_back(item);
    }
  }
  return filtered;
}

template<class T, class MatchFn, class SortFn>
void UpdateVersionedSnapshot(
  VersionedSnapshot<std::vector<T>>& snapshot, std::vector<T> items, bool force_increment, MatchFn match, SortFn sort
) {
  sort(items);

  static const std::vector<T> kEmpty;
  const std::vector<T>& current = snapshot.data ? *snapshot.data : kEmpty;
  if (force_increment || !VectorsEqual(items, current, match)) {
    ++snapshot.version;
    snapshot.data = MakeSharedSnapshot(std::move(items));
  } else if (!snapshot.data) {
    snapshot.data = MakeSharedSnapshot(std::move(items));
  }
}

} // namespace detail
} // namespace mcp
} // namespace giga_edge
