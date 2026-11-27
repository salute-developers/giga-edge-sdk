#pragma once

#include "giga_edge/lib/common/span.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <variant>
#include <vector>

namespace giga_edge::model::detail {

using TensorData = std::variant<
  std::vector<float>,
  std::vector<std::int32_t>,
  std::vector<int64_t>,
  Span<const float>,
  Span<const std::int32_t>,
  Span<const int64_t>,
  std::shared_ptr<const std::vector<float>>,
  std::shared_ptr<const std::vector<std::int32_t>>,
  std::shared_ptr<const std::vector<int64_t>>,
  std::vector<std::byte>,
  Span<const std::byte> // universal for recursive state saving
  >;

constexpr inline bool IsLightweightDeferrable(...) {
  return false;
}
template<typename T, typename = decltype(std::declval<T>().size())>
constexpr bool IsLightweightDeferrable(const T& val) {
  return val.size() == 0;
}
template<typename T>
constexpr bool IsLightweightDeferrable(const std::shared_ptr<T>&) {
  return true;
}
template<typename T>
constexpr bool IsLightweightDeferrable(std::vector<T>&&) {
  return true;
}
template<typename T>
constexpr bool IsLightweightDeferrable(const Span<T>& span) {
  return (span.data() == nullptr) || (span.size() == 0);
}
template<typename... Ts>
constexpr bool IsLightweightDeferrable(const std::variant<Ts...>& v) {
  return std::visit(
    [](const auto& val) {
      return IsLightweightDeferrable(val);
    },
    v
  );
}
template<typename T>
constexpr std::enable_if_t<std::is_same_v<std::decay_t<T>, TensorData>, bool> IsLightweightDeferrable(T&& v) {
  return std::visit(
    [](auto&& val) {
      return IsLightweightDeferrable(std::forward<decltype(val)>(val));
    },
    std::forward<T>(v)
  );
}

template<typename SpanLike>
constexpr auto ToSpan(const SpanLike& span) -> decltype(Span{span.data(), span.size()}) {
  return Span{span.data(), span.size()};
}
template<typename SpanLike>
constexpr auto ToSpan(const std::shared_ptr<SpanLike>& ptr) -> decltype(Span{ptr->data(), ptr->size()}) {
  return Span{ptr->data(), ptr->size()};
}

template<typename T, typename F>
constexpr decltype(auto) ApplyToSpan(const T& data, F&& f, const decltype(ToSpan(data))* = nullptr) {
  return std::forward<F>(f)(ToSpan(data));
}

template<typename... Ts, typename F>
constexpr decltype(auto) ApplyToSpan(const std::variant<Ts...>& data, F&& f) {
  return std::visit(
    [&f](const auto& val) {
      return ApplyToSpan(val, std::forward<F>(f));
    },
    data
  );
}

template<typename F>
constexpr decltype(auto) ApplyToSpan(const TensorData& data, F&& f) {
  return std::visit(
    [&f](const auto& val) {
      return ApplyToSpan(val, std::forward<F>(f));
    },
    data
  );
}

template<class T>
struct is_variant : std::false_type {}; // NOLINT(readability-identifier-naming): type trait, std-style is_* idiom
template<class... Args>
struct is_variant<std::variant<Args...>> : std::true_type {};
template<class T>
constexpr bool is_variant_v = is_variant<std::decay_t<T>>::value;

template<typename T>
constexpr std::size_t GetDataSize(const T& data) {
  return ApplyToSpan(data, [](const auto& span) {
    return span.size();
  });
}

template<typename T>
TensorData Defer(T&& data) {
  if (IsLightweightDeferrable(std::forward<T>(data))) {
    if constexpr (is_variant_v<T>) {
      return std::visit(
        [](auto&& v) -> TensorData {
          return std::forward<decltype(v)>(v);
        },
        // NOLINTNEXTLINE(bugprone-use-after-move): IsLightweightDeferrable() inspects only, never moves
        std::forward<T>(data)
      );
    } else {
      // NOLINTNEXTLINE(bugprone-use-after-move): IsLightweightDeferrable() inspects only, never moves
      return std::forward<T>(data);
    }
  }

  return ApplyToSpan(data, [](const auto& span) -> TensorData {
    return std::vector<typename std::decay_t<decltype(span)>::value_type>(span.data(), span.data() + span.size());
  });
}

} // namespace giga_edge::model::detail
