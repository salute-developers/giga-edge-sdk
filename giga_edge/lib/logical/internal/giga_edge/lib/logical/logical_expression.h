#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace giga_edge {

class ILogicalExpression {
public:
  virtual ~ILogicalExpression() = default;

  virtual bool Apply(const std::unordered_map<std::string, bool>& variables) = 0;
  virtual std::unordered_set<std::string_view> Variables() = 0;
};
using ILogicalExpressionPtr = std::shared_ptr<ILogicalExpression>;

ILogicalExpressionPtr ParseLogicalExpression(std::string_view expression);

} // namespace giga_edge