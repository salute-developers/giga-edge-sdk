#pragma once

#include "data_reader.h"

#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace giga_edge::data_reader::detail {

// Some std::enable_if below are redundant: they make the compiler errors clearer.
template<typename T, typename... U>
inline constexpr bool is_one_of_v = std::disjunction<std::is_same<T, U>..., std::false_type>::value;

template<typename T>
inline constexpr bool is_span_ptr_v = is_one_of_v<T, IMovableDataSpanPtr, IDataSpanPtr>;

template<typename T>
inline constexpr bool is_span_v = is_one_of_v<T, IMovableDataSpan, IDataSpan>;

template<typename T>
inline constexpr bool is_data_value_v = is_one_of_v<T, std::vector<char>, std::pair<const char*, size_t>>;

template<typename T, typename E = std::enable_if_t<is_span_v<T>>>
struct SpanTraits {};
template<>
struct SpanTraits<IMovableDataSpan, void> {
  using Ptr = IMovableDataSpanPtr;
};
template<>
struct SpanTraits<IDataSpan, void> {
  using Ptr = IDataSpanPtr;
};
template<typename T>
using SpanPtr = typename SpanTraits<T>::Ptr;

template<typename T>
using IfSpanPtr = std::enable_if_t<detail::is_span_ptr_v<T>, T>;

template<typename T>
struct IsOptional : std::false_type {};
template<typename T>
struct IsOptional<std::optional<T>> : std::true_type {};
template<typename T>
inline constexpr bool is_cvref_optional_v = IsOptional<std::remove_cv_t<std::remove_reference_t<T>>>::value;

template<typename T, typename E = void>
struct Factory {};

template<typename T>
struct Factory<T, std::void_t<SpanPtr<T>>> // for better diagnostics
  : Factory<SpanPtr<T>> {};

template<typename... U>
struct Factory<std::unique_ptr<U...>> {
  using Ptr = std::unique_ptr<U...>;

  template<typename T, typename... Args>
  static Ptr Create(Args&&... args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
  }
};

template<typename... U>
struct Factory<std::shared_ptr<U...>> {
  using Ptr = std::shared_ptr<U...>;

  template<typename T, typename... Args>
  static Ptr Create(Args&&... args) {
    return std::make_shared<T>(std::forward<Args>(args)...);
  }
};

} // namespace giga_edge::data_reader::detail