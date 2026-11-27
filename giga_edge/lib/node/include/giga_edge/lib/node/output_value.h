#pragma once

#include "giga_edge/lib/common/non_copyable.h"

#include <any>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>

namespace giga_edge {

struct ValueInfo {
  struct BatchInfo {
    uint32_t id;
    bool is_last;
  };

  std::deque<BatchInfo> batch_infos;

  uint32_t seq_id;

  bool is_recursive;

  std::any external_data;

  bool IsLastSeqInfo() const;
  std::string ToString() const;
};

class OutputValueBase : public NonCopyable {
public:
  explicit OutputValueBase(ValueInfo value_info);

  // Accessors kept snake_case: ValueInfo() would collide with the ValueInfo type, and
  // value() stays parallel to it (also avoids confusion with std::optional::value()).
  const ValueInfo& value_info() const; // NOLINT(readability-identifier-naming)

  ValueInfo& value_info(); // NOLINT(readability-identifier-naming)

  virtual ~OutputValueBase() = default;

private:
  ValueInfo value_info_;
};

template<class T>
class OutputValue : public OutputValueBase {
public:
  explicit OutputValue(T value, ValueInfo value_info = {})
    : OutputValueBase(std::move(value_info))
    , value_(std::move(value)) {}

  const T& value() const { // NOLINT(readability-identifier-naming)
    return value_;
  }

private:
  const T value_;
};

using OutputValueBasePtr = std::shared_ptr<OutputValueBase>;

} // namespace giga_edge