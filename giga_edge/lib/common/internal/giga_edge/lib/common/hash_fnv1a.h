// The FNV-1a algorithm has been put into the public domain by the authors.

#pragma once

#include <cstdint>
#include <iterator>
#include <type_traits>

namespace giga_edge {

namespace detail {

template<typename T>
inline constexpr bool is_fnv1a_byte_v = std::is_scalar_v<T> && (sizeof(T) == 1);
template<typename It>
inline constexpr bool is_fnv1a_byte_iterator_v = is_fnv1a_byte_v<typename std::iterator_traits<It>::value_type>;

template<typename T, T prime, typename It>
T AppendHashFNV1a(It first, It last, T hash) {
  static_assert(std::is_unsigned_v<T> && (sizeof(T) >= 4));
  static_assert(is_fnv1a_byte_iterator_v<It>);

  for (; first != last; ++first) {
    hash ^= static_cast<std::uint8_t>(*first);
    hash *= prime;
  }
  return hash;
}

template<typename T>
struct HashFNV1aTraits : std::false_type {};

template<>
struct HashFNV1aTraits<std::uint32_t> : std::true_type {
  static constexpr std::uint32_t basis = 2166136261u;
  static constexpr std::uint32_t prime = 0x01000193u;
};

template<>
struct HashFNV1aTraits<std::uint64_t> : std::true_type {
  static constexpr std::uint64_t basis = 14695981039346656037u;
  static constexpr std::uint64_t prime = 0x100000001b3u;
};

} // namespace detail

template<typename T, typename It>
std::enable_if_t<detail::HashFNV1aTraits<T>::value && detail::is_fnv1a_byte_iterator_v<It>, T> HashRangeFNV1a(
  It first, It last, T partial_hash = detail::HashFNV1aTraits<T>::basis
) {
  return detail::AppendHashFNV1a<T, detail::HashFNV1aTraits<T>::prime, It>(first, last, partial_hash);
}

template<typename T, typename Value>
std::enable_if_t<detail::HashFNV1aTraits<T>::value && std::is_scalar_v<Value>, T> HashValueFNV1a(
  const Value& value, T partial_hash = detail::HashFNV1aTraits<T>::basis
) {
  auto first = reinterpret_cast<const std::uint8_t*>(&value);
  return detail::AppendHashFNV1a<T, detail::HashFNV1aTraits<T>::prime>(first, first + sizeof(value), partial_hash);
}

} // namespace giga_edge
