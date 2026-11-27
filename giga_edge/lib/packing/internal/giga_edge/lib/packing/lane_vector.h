#pragma once

#include "giga_edge/lib/packing/check_enforcements.h"
#include "giga_edge/lib/packing/simd_trampoline.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#if GIGA_EDGE_PACKING_FORCE_ARM_NEON                                                                                   \
  || (GIGA_EDGE_PACKING_HAS_ARM_NEON && !GIGA_EDGE_PACKING_FORCE_VECTOR_EXTENSION && !GIGA_EDGE_PACKING_DISABLE_EXPLICIT_SIMD)
#define GIGA_EDGE_PACKING_USING_ARM_NEON 1
#include <arm_neon.h>
#endif // using arm neon

namespace giga_edge::packing {

namespace detail {

#if GIGA_EDGE_PACKING_FORCE_VECTOR_EXTENSION                                                                           \
  || (GIGA_EDGE_PACKING_HAS_VECTOR_EXTENSION && !GIGA_EDGE_PACKING_DISABLE_EXPLICIT_SIMD)

#define GIGA_EDGE_PACKING_USING_VECTOR_EXTENSION 1
using LaneVector __attribute__((vector_size(16))) = std::uint8_t;
using LaneType = std::uint8_t;
constexpr unsigned lane_count = 16;

#else // use vector extension

#if defined(__aarch64__) || defined(__x86_64__)
using LaneType = std::uint_fast64_t;
#else
using LaneType = std::uint_fast32_t;
#endif

constexpr unsigned lane_count = 16 / sizeof(LaneType);
struct LaneVector {
  LaneType v[lane_count];
  static_assert(sizeof(v) == 16);
};

constexpr auto& AccessLane(LaneVector& lv, unsigned i) {
  return lv.v[i];
}
constexpr const auto& AccessLane(const LaneVector& lv, unsigned i) {
  return lv.v[i];
}
template<typename T>
constexpr T& AccessLane(T& v, unsigned) {
  return v;
}

template<typename F, typename... Args>
constexpr auto& ForEachLane(F&& f, LaneVector& io, Args&... args) {
  for (unsigned i = 0; i < std::size(io.v); ++i) {
    std::forward<F>(f)(io.v[i], AccessLane(args, i)...);
  }
  return io;
}

#define GIGA_EDGE_PACKING_DEFINE_ASSIGN_OP_(op, ...)                                                                   \
  constexpr auto& operator op(LaneVector& a, __VA_ARGS__ b) {                                                          \
    return ForEachLane(                                                                                                \
      [](auto& a, auto& b) constexpr {                                                                                 \
        a op b;                                                                                                        \
      },                                                                                                               \
      a,                                                                                                               \
      b                                                                                                                \
    );                                                                                                                 \
  }

#define GIGA_EDGE_PACKING_DEFINE_COUPLED_OP_(op, ...)                                                                  \
  constexpr auto operator op(const LaneVector& a, __VA_ARGS__ b) {                                                     \
    auto out = a;                                                                                                      \
    out op## = b;                                                                                                      \
    return out;                                                                                                        \
  }

#define GIGA_EDGE_PACKING_DEFINE_OP_(op, ...)                                                                          \
  GIGA_EDGE_PACKING_DEFINE_ASSIGN_OP_(op## =, __VA_ARGS__)                                                             \
  GIGA_EDGE_PACKING_DEFINE_COUPLED_OP_(op, __VA_ARGS__)

// Define useful operator overloads.
GIGA_EDGE_PACKING_DEFINE_OP_(|, const LaneVector&);
GIGA_EDGE_PACKING_DEFINE_OP_(&, const LaneVector&);
GIGA_EDGE_PACKING_DEFINE_OP_(<<, const unsigned);
GIGA_EDGE_PACKING_DEFINE_OP_(>>, const unsigned);

constexpr auto operator~(const LaneVector& a) {
  LaneVector out{};
  return ForEachLane(
    [](auto& out, auto& a) constexpr {
      out = ~a;
    },
    out,
    a
  );
}

#undef GIGA_EDGE_PACKING_DEFINE_OP_
#undef GIGA_EDGE_PACKING_DEFINE_COUPLED_OP_
#undef GIGA_EDGE_PACKING_DEFINE_ASSIGN_OP_

#endif // use vector extension

template<typename T>
struct LaneVectorConstructor;
template<std::size_t... ids>
struct LaneVectorConstructor<std::index_sequence<ids...>> {
  constexpr static auto Make(LaneType scalar) {
    auto ident = [scalar](auto&&) constexpr {
      return scalar;
    };
    return LaneVector{ident(ids)...};
  }
};

} // namespace detail

using detail::lane_count;
using detail::LaneType;
using detail::LaneVector;

template<typename T>
constexpr std::enable_if_t<std::is_integral_v<T>, T> DupExpandByte(std::uint8_t b) {
  std::make_unsigned_t<T> out = 0;
  for (auto i = sizeof(T); i; --i) {
    if constexpr (sizeof(T) > 1) { // inhibit warning
      out <<= 8;
    }
    out |= b;
  }
  return static_cast<T>(out);
}

constexpr LaneVector MakeLaneVector(LaneType scalar) {
  return detail::LaneVectorConstructor<std::make_index_sequence<lane_count>>::Make(scalar);
}

constexpr LaneVector MakeLaneVectorDupByte(std::uint8_t b) {
  return MakeLaneVector(DupExpandByte<LaneType>(b));
}

} // namespace giga_edge::packing
