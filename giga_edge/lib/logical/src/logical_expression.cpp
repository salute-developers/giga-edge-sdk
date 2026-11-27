#include "giga_edge/lib/logical/logical_expression.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace giga_edge {
namespace {

class VariableLogicalExpr : public ILogicalExpression {
public:
  explicit VariableLogicalExpr(std::string id)
    : id_(std::move(id)) {}

  bool Apply(const std::unordered_map<std::string, bool>& variables) override {
    return variables.at(id_);
  }

  std::unordered_set<std::string_view> Variables() override {
    return {id_};
  }

private:
  std::string id_;
};

class LogicalOpNot : public ILogicalExpression {
public:
  explicit LogicalOpNot(ILogicalExpressionPtr expr)
    : expr_(std::move(expr)) {}

  bool Apply(const std::unordered_map<std::string, bool>& variables) override {
    return !expr_->Apply(variables);
  }

  std::unordered_set<std::string_view> Variables() override {
    return expr_->Variables();
  }

private:
  ILogicalExpressionPtr expr_;
};

class LogicalOpAnd : public ILogicalExpression {
public:
  LogicalOpAnd(ILogicalExpressionPtr l, ILogicalExpressionPtr r)
    : l_(std::move(l))
    , r_(std::move(r)) {}

  bool Apply(const std::unordered_map<std::string, bool>& variables) override {
    return l_->Apply(variables) && r_->Apply(variables);
  }

  std::unordered_set<std::string_view> Variables() override {
    auto result = l_->Variables();
    result.merge(r_->Variables());
    return result;
  }

private:
  ILogicalExpressionPtr l_;
  ILogicalExpressionPtr r_;
};

class LogicalOpOr : public ILogicalExpression {
public:
  LogicalOpOr(ILogicalExpressionPtr l, ILogicalExpressionPtr r)
    : l_(std::move(l))
    , r_(std::move(r)) {}

  bool Apply(const std::unordered_map<std::string, bool>& variables) override {
    return l_->Apply(variables) || r_->Apply(variables);
  }

  std::unordered_set<std::string_view> Variables() override {
    auto result = l_->Variables();
    result.merge(r_->Variables());
    return result;
  }

private:
  ILogicalExpressionPtr l_;
  ILogicalExpressionPtr r_;
};

class ExpressionParser {
public:
  explicit ExpressionParser(std::string_view expression)
    : expression_(expression)
    , pos_(0) {}

  ILogicalExpressionPtr Parse() {
    auto result = ParseUpToOr();
    if (pos_ != expression_.size()) {
      throw std::invalid_argument("Invalid expression: " + std::string{expression_});
    }
    return result;
  }

private:
  void ConsumeSpaces() {
    while (pos_ < expression_.size() && std::isspace(static_cast<unsigned char>(expression_[pos_]))) {
      ++pos_;
    }
  }

  bool Consume(std::string_view prefix) {
    ConsumeSpaces();
    if (expression_.substr(pos_, prefix.size()) == prefix) {
      pos_ += prefix.size();
      return true;
    }
    return false;
  }

  bool ConsumeAnyOf(const std::vector<std::string_view>& prefixes) {
    for (const auto& prefix : prefixes) {
      if (Consume(prefix)) {
        return true;
      }
    }
    return false;
  }

  std::string_view ConsumeVariable() {
    ConsumeSpaces();
    int start = pos_;
    while (pos_ < expression_.size() && (std::isalnum(expression_[pos_]) || expression_[pos_] == '_')) {
      ++pos_;
    }
    if (start == pos_) {
      throw std::invalid_argument("Invalid expression: " + std::string{expression_});
    }
    auto variable = expression_.substr(start, pos_ - start);
    if (banned_variables_.find(variable) != banned_variables_.end()) {
      throw std::invalid_argument("Invalid expression: " + std::string{expression_});
    }
    return variable;
  }

  ILogicalExpressionPtr ParseUpToOr() {
    auto left = ParseUpToAnd();
    while (ConsumeAnyOf({"or ", "||", "|"})) {
      auto right = ParseUpToAnd();
      left = std::make_shared<LogicalOpOr>(left, right);
    }
    return left;
  }

  ILogicalExpressionPtr ParseUpToAnd() {
    auto left = ParseUpToNot();
    while (ConsumeAnyOf({"and ", "&&", "&"})) {
      auto right = ParseUpToNot();
      left = std::make_shared<LogicalOpAnd>(left, right);
    }
    return left;
  }

  ILogicalExpressionPtr ParseUpToNot() {
    if (ConsumeAnyOf({"not ", "!"})) {
      auto inner = ParseUpToNot();
      return std::make_shared<LogicalOpNot>(inner);
    }
    return ParseUpToBrackets();
  }

  ILogicalExpressionPtr ParseUpToBrackets() {
    if (Consume("(")) {
      auto inner = ParseUpToOr();
      if (!Consume(")")) {
        throw std::invalid_argument("Invalid expression: " + std::string{expression_});
      }
      return inner;
    }
    return ParseUpToVariables();
  }

  ILogicalExpressionPtr ParseUpToVariables() {
    auto id = ConsumeVariable();
    return std::make_shared<VariableLogicalExpr>(std::string{id});
  }

private:
  std::string_view expression_;
  int pos_;

  const std::unordered_set<std::string_view> banned_variables_ = {"and", "or", "not"};
};

} // namespace

ILogicalExpressionPtr ParseLogicalExpression(std::string_view expression) {
  return ExpressionParser(expression).Parse();
}

} // namespace giga_edge
