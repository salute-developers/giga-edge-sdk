#pragma once

#include "giga_edge/lib/common/non_copyable.h"

#include <utility>

namespace giga_edge {

template<class F>
class ScopeGuard : public NonCopyable {
public:
  explicit ScopeGuard(F func)
    : func_(std::move(func)) {}
  ~ScopeGuard() {
    func_();
  }

private:
  F func_;
};

} // namespace giga_edge
