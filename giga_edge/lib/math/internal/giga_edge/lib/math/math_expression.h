#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace giga_edge {

class IMathExpression {
public:
  virtual ~IMathExpression() = default;

  virtual float Apply(const std::unordered_map<std::string, float>& variables) = 0;
  virtual std::unordered_set<std::string_view> Variables() = 0;
};
using IMathExpressionPtr = std::shared_ptr<IMathExpression>;

IMathExpressionPtr ParseMathExpression(std::string_view expression);

} // namespace giga_edge
