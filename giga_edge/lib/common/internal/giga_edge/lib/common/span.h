#pragma once

#include <cstddef>
#include <type_traits>

namespace giga_edge {

template<typename T>
class Span {
public:
  using value_type = std::remove_cv_t<T>;
  using pointer = T*;
  using size_type = std::size_t;

  Span(T* data, std::size_t size)
    : data_(data)
    , size_(size) {}
  Span() = default;
  Span(const Span&) = default;

  template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
  // NOLINTNEXTLINE(google-explicit-constructor): implicit Span<T> -> Span<const T>, as in std::span
  Span(const Span<U>& other)
    : data_(other.data_)
    , size_(other.size_) {}

  pointer data() const { // NOLINT(readability-identifier-naming): std::span-like API
    return data_;
  }
  size_type size() const { // NOLINT(readability-identifier-naming): std::span-like API
    return size_;
  }

private:
  T* data_ = nullptr;
  std::size_t size_ = 0;

  template<typename U>
  friend class Span;
};

} // namespace giga_edge
