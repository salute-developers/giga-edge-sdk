#pragma once

#include "enum_fwd.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace giga_edge::util::detail {

// C++20 constexpr functions missing in C++17.

template<typename T>
struct Identity {
  using Type = T;
};
template<typename T>
using TypeIdentity = typename Identity<T>::Type;

template<typename Enum>
constexpr auto ToUnderlying(Enum value) {
  return static_cast<std::underlying_type_t<Enum>>(value);
}

template<typename T, typename Less>
constexpr bool IsSorted(T* arr, const std::size_t size, Less less) {
  for (std::size_t i = 1; i < size; ++i) {
    if (less(arr[i], arr[i - 1])) {
      return false;
    }
  }
  return true;
}

// Insertion sort: std::sort is not constexpr before C++20, and enum tables are small.
template<typename T, typename Less>
constexpr void SlowSort(T* arr, const std::size_t size, Less less) {
  if (IsSorted(arr, size, less)) {
    return;
  }

  for (std::size_t i = 1; i < size; ++i) {
    auto val = std::move(arr[i]);
    std::size_t j = i;
    for (; j && less(val, arr[j - 1]); --j) {
      arr[j] = std::move(arr[j - 1]);
    }
    arr[j] = std::move(val);
  }
}

template<typename T, typename Less>
constexpr void SortUnique(T* arr, const std::size_t size, Less less) {
  SlowSort(arr, size, less);

  for (std::size_t i = 1; i < size; ++i) {
    if (!less(arr[i - 1], arr[i])) {
      throw std::invalid_argument("Enum values or value names are not unique");
    }
  }
}

constexpr inline bool SizeFirstIsStringLess(const std::string_view& a, const std::string_view& b) {
  if (a.size() != b.size()) { // comparing sizes first is cheaper than comparing the text
    return a.size() < b.size();
  }
  return a < b;
}

template<typename Underlying>
struct MultiNameEnumValue {
  const Underlying value;
  const std::initializer_list<const char*> names;
};

struct EnumDescriptorParams {
  const std::size_t value_count;
  const std::size_t name_count;
};

// Returns false if VALUE_COUNT_ is available and not equal to the function argument.
template<typename Enum, auto value_count = Enum::VALUE_COUNT_>
constexpr bool ValidateEnumValueCount(std::initializer_list<MultiNameEnumValue<Enum>> descr) {
  for (const auto& [value, names] : descr) {
    if (value_count == value) {
      return true; // if explicitly described
    }
  }
  return static_cast<std::size_t>(value_count) == descr.size();
}

template<typename Enum>
constexpr bool ValidateEnumValueCount(...) {
  return true;
}

template<typename Enum>
constexpr EnumDescriptorParams GetEnumDescriptorParams(std::initializer_list<MultiNameEnumValue<Enum>> descr) {
  if (!ValidateEnumValueCount<Enum>(descr)) {
    throw std::invalid_argument("Not all enum values are defined");
  }

  std::size_t name_count = 0;
  for (const auto& [value, names] : descr) {
    name_count += names.size();

    if (std::empty(names)) {
      throw std::invalid_argument("The enum value without a name");
    }
  }

  return {descr.size(), name_count};
}

// Use underlying type to help the compiler to reduce the number of instantiated runtime functions.
template<typename Enum>
using PromotedUnderlying = decltype(static_cast<std::underlying_type_t<Enum>>(0) + 0);

template<typename Underlying>
struct EnumValueName {
  std::string_view name;
  Underlying value;
};

struct IsLessByValue {
  template<typename Underlying>
  constexpr bool operator()(const EnumValueName<Underlying>& a, const EnumValueName<Underlying>& b) const {
    return a.value < b.value;
  }

  template<typename Underlying>
  constexpr bool operator()(const EnumValueName<Underlying>& a, Underlying b) const {
    return a.value < b;
  }
};

struct IsLessByName {
  template<typename Underlying>
  constexpr bool operator()(const EnumValueName<Underlying>& a, const EnumValueName<Underlying>& b) {
    return SizeFirstIsStringLess(a.name, b.name);
  }

  template<typename Underlying>
  constexpr bool operator()(const EnumValueName<Underlying>& a, const std::string_view& b) {
    return SizeFirstIsStringLess(a.name, b);
  }
};

template<typename Underlying>
constexpr bool IsSequentional(const EnumValueName<Underlying>* arr, std::size_t size) {
  for (std::size_t i = 1; i < size; ++i) {
    if (i != static_cast<std::size_t>(arr[i].value - arr[0].value)) {
      return false;
    }
  }
  return true;
}

// Stores weak pointers.
template<typename Enum, std::size_t result_value_count, typename IsLess>
constexpr auto ToNameValueArray(std::initializer_list<MultiNameEnumValue<Enum>> in) {
  std::array<EnumValueName<PromotedUnderlying<Enum>>, result_value_count> out{};
  std::size_t i = 0;
  for (const auto& [value, names] : in) {
    for (const auto& name : names) {
      out[i++] = {name, ToUnderlying(value)};
      if (result_value_count == in.size()) { // only the first name; GetEnumDescriptorParams rejects empty names
        break;
      }
    }
  }
  SortUnique(out.data(), out.size(), IsLess());
  return out;
}

template<typename T, std::size_t n, typename B>
struct BiasedArray : std::array<T, n> { // a name table for sequential values; bias is the first value
  B bias{};
};

template<typename T, std::size_t n, typename B>
constexpr B GetBias(const BiasedArray<T, n, B>& arr) {
  return arr.bias;
}

template<typename T, std::size_t n>
constexpr signed char GetBias([[maybe_unused]] const std::array<T, n>& arr) {
  return 0;
}

// Stores weak pointers.
template<typename Underlying, std::size_t n>
constexpr auto ToNameArray(const std::array<EnumValueName<Underlying>, n>& in) {
  BiasedArray<const char*, n, Underlying> out{{}, n ? in[0].value : 0};
  for (std::size_t i = 0; i < n; ++i) {
    out[i] = in[i].name.data(); // '\0' terminated by construction
  }
  return out;
}

template<typename Underlying>
inline std::string_view EnumValueToString(
  const char* const* table, const std::size_t table_size, Underlying value, const TypeIdentity<Underlying> bias
) {
  value -= bias;
  if ((value < 0) || (value >= table_size)) {
    throw std::out_of_range("Enum value is out of range: " + std::to_string(value));
  }
  return table[value];
}

template<typename Underlying>
inline std::string_view EnumValueToString(
  const EnumValueName<Underlying>* table,
  const std::size_t table_size,
  TypeIdentity<Underlying> value,
  const TypeIdentity<Underlying> // compatibility bias argument
) {
  const auto table_end = table + table_size;
  const auto found = std::lower_bound(table, table_end, value, IsLessByValue());
  if ((found == table_end) || (found->value != value)) {
    throw std::out_of_range("Not a enum value: " + std::to_string(value));
  }

  return found->name;
}

inline std::size_t EnumStringToValue(const char* const* table, const std::size_t table_size, std::string_view name) {
  const auto table_end = table + table_size;
  auto found = std::find(table, table_end, name);
  if (found == table_end) {
    throw std::out_of_range("No enum value for string: " + std::string(name));
  }
  return std::distance(table, found);
}

template<typename Underlying>
Underlying EnumStringToValue(
  const EnumValueName<Underlying>* table, const std::size_t table_size, std::string_view name
) {
  const auto table_end = table + table_size;
  const auto found = std::lower_bound(table, table_end, name, IsLessByName());
  if ((found == table_end) || (found->name != name)) {
    throw std::out_of_range("No enum value for string: " + std::string(name));
  }

  return found->value;
}

} // namespace giga_edge::util::detail

#define GIGA_EDGE_ENUM_MAPPER_OPEN_NAMESPACE_() namespace giga_edge {
#define GIGA_EDGE_ENUM_MAPPER_CLOSE_NAMESPACE_() }

#define GIGA_EDGE_ENUM_VALUE_TO_NAME_MAPPER_(enum_type, ...)                                                           \
  template<>                                                                                                           \
  std::string_view EnumToString(enum_type value) { /* not inline */                                                    \
    namespace ns = ::giga_edge::util::detail;                                                                          \
    static constexpr auto table = __VA_ARGS__;                                                                         \
    return ns::EnumValueToString(table.data(), table.size(), ns::ToUnderlying(value), ns::GetBias(table));             \
  }

#define GIGA_EDGE_ENUM_NAME_TO_VALUE_MAPPER_(enum_type, ...)                                                           \
  template<>                                                                                                           \
  enum_type EnumFromString(const std::string_view name) { /* not inline */                                             \
    namespace ns = ::giga_edge::util::detail;                                                                          \
    static constexpr auto table = __VA_ARGS__;                                                                         \
    return static_cast<enum_type>(ns::EnumStringToValue(table.data(), table.size(), name));                            \
  }

// Usage:
//   Given an enum with sequential values
//     enum MyE { EVal1 = 0, EVal2, EVal3, ... };
//   define converter methods by invoking the macro in the GLOBAL namespace
//     GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(MyE, "EVal1 string", "EVal2 string", "EVal3 string", ...)
#define GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(enum_type, ...)                                                           \
  GIGA_EDGE_ENUM_MAPPER_OPEN_NAMESPACE_()                                                                              \
  GIGA_EDGE_ENUM_VALUE_TO_NAME_MAPPER_(enum_type, std::array{__VA_ARGS__})                                             \
  GIGA_EDGE_ENUM_NAME_TO_VALUE_MAPPER_(enum_type, std::array{__VA_ARGS__})                                             \
  GIGA_EDGE_ENUM_MAPPER_CLOSE_NAMESPACE_()

#define GIGA_EDGE_COMPLEX_ENUM_MAPPER_GET_TABLE_PROLOG_(enum_type, ...)                                                \
  constexpr auto apply = [](auto&& f, auto&&... args) -> decltype(auto) {                                              \
    return std::forward<decltype(f)>(f)({__VA_ARGS__}, std::forward<decltype(args)>(args)...);                         \
  };                                                                                                                   \
  constexpr auto params = apply(util::detail::GetEnumDescriptorParams<enum_type>);

#define GIGA_EDGE_COMPLEX_ENUM_MAPPER_GET_VALUE_TABLE_(enum_type, ...)                                                 \
  []() {                                                                                                               \
    GIGA_EDGE_COMPLEX_ENUM_MAPPER_GET_TABLE_PROLOG_(enum_type, __VA_ARGS__)                                            \
    constexpr auto gcc9_workaround = &ns::ToNameValueArray<enum_type, params.value_count, ns::IsLessByValue>;          \
    constexpr auto table = apply(gcc9_workaround);                                                                     \
    if constexpr (ns::IsSequentional(table.data(), table.size())) {                                                    \
      return ns::ToNameArray(table);                                                                                   \
    } else {                                                                                                           \
      return table;                                                                                                    \
    }                                                                                                                  \
  }()

#define GIGA_EDGE_COMPLEX_ENUM_MAPPER_GET_NAME_TABLE_(enum_type, ...)                                                  \
  []() {                                                                                                               \
    GIGA_EDGE_COMPLEX_ENUM_MAPPER_GET_TABLE_PROLOG_(enum_type, __VA_ARGS__)                                            \
    constexpr auto gcc9_workaround = &ns::ToNameValueArray<enum_type, params.name_count, ns::IsLessByName>;            \
    return apply(gcc9_workaround);                                                                                     \
  }()

// Usage:
//  Given a generic enum
//    enum MyE { EVal1 = 0, EVal2 = <x>, EVal3, ... };
//  define converter methods by invoking the macro in the GLOBAL namespace
//    GIGA_EDGE_DEFINE_COMPLEX_ENUM_MAPPING(MyE,
//      {EVal1, {"EVal1 string1", "Eval1 string2"}},
//      {EVal2, {"EVal2 string"}},
//      {EVal3, {"EVal3 string1", "EVal3 string2", "EVal3 string3", ...}}, ...
//    )
//  If enum has VALUE_COUNT_ value which is not included in the provided value descriptor list,
//  this value will be used to validate the number of provided descriptors.
#define GIGA_EDGE_DEFINE_COMPLEX_ENUM_MAPPING(enum_type, ...)                                                          \
  GIGA_EDGE_ENUM_MAPPER_OPEN_NAMESPACE_()                                                                              \
  GIGA_EDGE_ENUM_VALUE_TO_NAME_MAPPER_(                                                                                \
    enum_type, GIGA_EDGE_COMPLEX_ENUM_MAPPER_GET_VALUE_TABLE_(enum_type, __VA_ARGS__)                                  \
  )                                                                                                                    \
  GIGA_EDGE_ENUM_NAME_TO_VALUE_MAPPER_(                                                                                \
    enum_type, GIGA_EDGE_COMPLEX_ENUM_MAPPER_GET_NAME_TABLE_(enum_type, __VA_ARGS__)                                   \
  )                                                                                                                    \
  GIGA_EDGE_ENUM_MAPPER_CLOSE_NAMESPACE_()
