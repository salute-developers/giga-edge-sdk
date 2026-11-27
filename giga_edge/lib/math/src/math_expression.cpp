#include "giga_edge/lib/math/math_expression.h"

#include <cctype>
#include <cmath>
#include <stdexcept>
#include <string>

namespace giga_edge {
namespace {

class NumberMathExpr : public IMathExpression {
public:
  explicit NumberMathExpr(float value)
    : value_(value) {}

  float Apply(const std::unordered_map<std::string, float>&) override {
    return value_;
  }

  std::unordered_set<std::string_view> Variables() override {
    return {};
  }

private:
  float value_;
};

class VariableMathExpr : public IMathExpression {
public:
  explicit VariableMathExpr(std::string id)
    : id_(std::move(id)) {}

  float Apply(const std::unordered_map<std::string, float>& variables) override {
    return variables.at(id_);
  }

  std::unordered_set<std::string_view> Variables() override {
    return {id_};
  }

private:
  std::string id_;
};

class MathOpNegate : public IMathExpression {
public:
  explicit MathOpNegate(IMathExpressionPtr expr)
    : expr_(std::move(expr)) {}

  float Apply(const std::unordered_map<std::string, float>& variables) override {
    return -expr_->Apply(variables);
  }

  std::unordered_set<std::string_view> Variables() override {
    return expr_->Variables();
  }

private:
  IMathExpressionPtr expr_;
};

class MathFunction : public IMathExpression {
public:
  using Function = float (*)(float);

  MathFunction(Function function, IMathExpressionPtr argument)
    : function_(function)
    , argument_(std::move(argument)) {}

  float Apply(const std::unordered_map<std::string, float>& variables) override {
    return function_(argument_->Apply(variables));
  }

  std::unordered_set<std::string_view> Variables() override {
    return argument_->Variables();
  }

private:
  Function function_;
  IMathExpressionPtr argument_;
};

float Ceil(float value) {
  return std::ceil(value);
}
float Floor(float value) {
  return std::floor(value);
}

class MathBinaryOp : public IMathExpression {
public:
  using Op = float (*)(float, float);

  MathBinaryOp(Op op, IMathExpressionPtr l, IMathExpressionPtr r)
    : op_(op)
    , l_(std::move(l))
    , r_(std::move(r)) {}

  float Apply(const std::unordered_map<std::string, float>& variables) override {
    return op_(l_->Apply(variables), r_->Apply(variables));
  }

  std::unordered_set<std::string_view> Variables() override {
    auto result = l_->Variables();
    result.merge(r_->Variables());
    return result;
  }

private:
  Op op_;
  IMathExpressionPtr l_;
  IMathExpressionPtr r_;
};

float Add(float l, float r) {
  return l + r;
}
float Subtract(float l, float r) {
  return l - r;
}
float Multiply(float l, float r) {
  return l * r;
}
float Divide(float l, float r) {
  return l / r;
}

class ExpressionParser {
public:
  explicit ExpressionParser(std::string_view expression)
    : expression_(expression)
    , pos_(0) {}

  IMathExpressionPtr Parse() {
    auto result = ParseUpToAdditive();
    ConsumeSpaces();
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

  bool Consume(char symbol) {
    ConsumeSpaces();
    if (pos_ < expression_.size() && expression_[pos_] == symbol) {
      ++pos_;
      return true;
    }
    return false;
  }

  IMathExpressionPtr ParseUpToAdditive() {
    auto left = ParseUpToMultiplicative();
    while (true) {
      if (Consume('+')) {
        left = std::make_shared<MathBinaryOp>(Add, left, ParseUpToMultiplicative());
      } else if (Consume('-')) {
        left = std::make_shared<MathBinaryOp>(Subtract, left, ParseUpToMultiplicative());
      } else {
        return left;
      }
    }
  }

  IMathExpressionPtr ParseUpToMultiplicative() {
    auto left = ParseUpToUnary();
    while (true) {
      if (Consume('*')) {
        left = std::make_shared<MathBinaryOp>(Multiply, left, ParseUpToUnary());
      } else if (Consume('/')) {
        left = std::make_shared<MathBinaryOp>(Divide, left, ParseUpToUnary());
      } else {
        return left;
      }
    }
  }

  IMathExpressionPtr ParseUpToUnary() {
    if (Consume('-')) {
      return std::make_shared<MathOpNegate>(ParseUpToUnary());
    }
    return ParseUpToBrackets();
  }

  IMathExpressionPtr ParseUpToBrackets() {
    if (Consume('(')) {
      auto inner = ParseUpToAdditive();
      if (!Consume(')')) {
        throw std::invalid_argument("Invalid expression: " + std::string{expression_});
      }
      return inner;
    }
    return ParseUpToOperands();
  }

  IMathExpressionPtr ParseUpToOperands() {
    ConsumeSpaces();
    if (pos_ < expression_.size() && std::isdigit(static_cast<unsigned char>(expression_[pos_]))) {
      return ParseNumber();
    }
    return ParseVariable();
  }

  IMathExpressionPtr ParseNumber() {
    size_t start = pos_;
    ConsumeDigits();
    if (pos_ < expression_.size() && expression_[pos_] == '.') {
      ++pos_;
      size_t fraction_start = pos_;
      ConsumeDigits();
      if (fraction_start == pos_) {
        throw std::invalid_argument("Invalid expression: " + std::string{expression_});
      }
    }
    return std::make_shared<NumberMathExpr>(std::stof(std::string{expression_.substr(start, pos_ - start)}));
  }

  void ConsumeDigits() {
    while (pos_ < expression_.size() && std::isdigit(static_cast<unsigned char>(expression_[pos_]))) {
      ++pos_;
    }
  }

  IMathExpressionPtr ParseVariable() {
    size_t start = pos_;
    while (pos_ < expression_.size()
           && (std::isalnum(static_cast<unsigned char>(expression_[pos_])) || expression_[pos_] == '_')) {
      ++pos_;
    }
    if (start == pos_) {
      throw std::invalid_argument("Invalid expression: " + std::string{expression_});
    }
    const auto name = expression_.substr(start, pos_ - start);
    if (Consume('(')) {
      return ParseFunction(name);
    }
    return std::make_shared<VariableMathExpr>(std::string{name});
  }

  IMathExpressionPtr ParseFunction(std::string_view name) {
    MathFunction::Function function = nullptr;
    if (name == "ceil") {
      function = Ceil;
    } else if (name == "floor") {
      function = Floor;
    } else {
      throw std::invalid_argument("Unknown function '" + std::string{name} + "' in: " + std::string{expression_});
    }
    auto argument = ParseUpToAdditive();
    if (!Consume(')')) {
      throw std::invalid_argument("Invalid expression: " + std::string{expression_});
    }
    return std::make_shared<MathFunction>(function, std::move(argument));
  }

private:
  std::string_view expression_;
  size_t pos_;
};

} // namespace

IMathExpressionPtr ParseMathExpression(std::string_view expression) {
  return ExpressionParser(expression).Parse();
}

} // namespace giga_edge
