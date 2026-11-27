#pragma once
#include "giga_edge/modules/function_calling/compact_tool_result.h"
#include "giga_edge/modules/smart_home_ha/catalog.h"
namespace giga_edge::ha {
struct Completion {
  CompactToolResults results;
  std::vector<McpResolvedAction> continuations;
};
McpResolvedAction ResolveAction(const AssistantResult& result, const Catalog& catalog);
Completion Complete(const McpActionCompletion& completion);
bool NeedsContinuation(const McpResolvedAction& action);
bool MatchesTarget(const McpResolvedAction& action, const Catalog& catalog);
} // namespace giga_edge::ha
