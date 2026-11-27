#pragma once

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace giga_edge {

// Holds a strong reference to a data span.
class IDataSpan {
public:
  virtual std::pair<const char*, size_t> Get() = 0;
  std::string_view View() {
    auto data = Get();
    return std::string_view(data.first, data.second);
  }

  virtual ~IDataSpan() = default;
};

// Holds a strong reference to a data span.
class IMovableDataSpan : public IDataSpan {
public:
  // Not thread-safe, does not synchronize with Get().
  // Calling Get() after Extract() is undefined behaviour.
  virtual std::vector<char> Extract() = 0;
};

using IDataSpanPtr = std::shared_ptr<IDataSpan>;
using IMovableDataSpanPtr = std::unique_ptr<IMovableDataSpan>;

} // namespace giga_edge