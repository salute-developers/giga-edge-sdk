#include "giga_edge/lib/packing/5bit.h"

#include "giga_edge/lib/packing/lane_vector.h"

#include <cassert>
#include <cstring>
#include <type_traits>
#include <utility>

#if defined(__clang__) || defined(__GNUC__)
#define GIGA_EDGE_RESTRICT_ __restrict
#else
#define GIGA_EDGE_RESTRICT_
#endif

namespace giga_edge::packing {

namespace {

constexpr std::size_t unpacked_block_size = sizeof(LaneVector) * 8;
constexpr std::size_t packed_block_size = sizeof(LaneVector) * 5;
constexpr std::size_t unpacked_tail_block_size = 8;
constexpr std::size_t packed_tail_block_size = 5;

template<typename T = LaneVector>
inline T ReadUnalignedInc(const std::uint8_t* GIGA_EDGE_RESTRICT_& GIGA_EDGE_RESTRICT_ src) {
  static_assert(std::is_trivially_copyable_v<T>);
  T v;
  std::memcpy(&v, src, sizeof(T));
  src += sizeof(T);
  return v;
}

template<typename T>
inline void WriteUnalignedInc(const T& v, std::uint8_t* GIGA_EDGE_RESTRICT_& GIGA_EDGE_RESTRICT_ dst) {
  static_assert(std::is_trivially_copyable_v<T>);
  std::memcpy(dst, &v, sizeof(T));
  dst += sizeof(T);
}

// 128 -> 80 bytes.
void Pack5BitMainBlock(const std::uint8_t* GIGA_EDGE_RESTRICT_ src, std::uint8_t* GIGA_EDGE_RESTRICT_ dst) {
  constexpr auto significant_mask = MakeLaneVectorDupByte(0b11111000u);
  constexpr auto lower_mask = MakeLaneVectorDupByte(0b00000111u);
  constexpr auto top_mask = MakeLaneVectorDupByte(0b11000000u);

  auto single = MakeLaneVector(0);
  auto single_ptr = dst;
  dst += sizeof(LaneVector);
  for (unsigned i = 0; i < 4; ++i) {
    auto first = ReadUnalignedInc(src);
    auto second = ReadUnalignedInc(src);
    single >>= 2;
    single |= (second << 3) & top_mask;
    auto v = (first & significant_mask) | ((second >> 5) & lower_mask);
    WriteUnalignedInc(v, dst);
  }
  WriteUnalignedInc(single, single_ptr);
}

#if GIGA_EDGE_PACKING_USING_ARM_NEON

// 80 -> 128 bytes.
void Unpack5BitMainBlockNeon(const uint8x16_t* GIGA_EDGE_RESTRICT_ src, uint8x16_t* GIGA_EDGE_RESTRICT_ dst) {
  const auto significant_mask = vdupq_n_u8(0b11111000);
  const auto lower_bits_mask = vdupq_n_u8(0b00011000);

  auto multi = *src++;

#define GIGA_EDGE_UNPACK_ROUND_(dir, amount)                                                                           \
  {                                                                                                                    \
    auto pack = *src++;                                                                                                \
    *dst++ = vandq_u8(pack, significant_mask);                                                                         \
    *dst++ = vsliq_n_u8(vandq_u8(vsh##dir##q_n_u8(multi, (amount)), lower_bits_mask), pack, 5);                        \
  }

  GIGA_EDGE_UNPACK_ROUND_(l, 3)
  GIGA_EDGE_UNPACK_ROUND_(l, 1)
  GIGA_EDGE_UNPACK_ROUND_(r, 1)
  GIGA_EDGE_UNPACK_ROUND_(r, 3)

#undef GIGA_EDGE_UNPACK_ROUND_
}

#else // using neon

// 80 -> 128 bytes.
void Unpack5BitMainBlock(const std::uint8_t* GIGA_EDGE_RESTRICT_ src, std::uint8_t* GIGA_EDGE_RESTRICT_ dst) {
  static constexpr auto significant_mask = MakeLaneVectorDupByte(0b11111000u);
  static constexpr auto most_mask = MakeLaneVectorDupByte(0b11100000u);
  static constexpr auto least_mask = MakeLaneVectorDupByte(0b00011000u);

  auto multi = ReadUnalignedInc(src);

#define GIGA_EDGE_UNPACK_ROUND_(dir, amount)                                                                           \
  {                                                                                                                    \
    auto pack = ReadUnalignedInc(src);                                                                                 \
    WriteUnalignedInc(pack & significant_mask, dst);                                                                   \
    auto v = ((pack << 5) & most_mask) | ((multi dir(amount)) & least_mask);                                           \
    WriteUnalignedInc(v, dst);                                                                                         \
  }

  GIGA_EDGE_UNPACK_ROUND_(<<, 3)
  GIGA_EDGE_UNPACK_ROUND_(<<, 1)
  GIGA_EDGE_UNPACK_ROUND_(>>, 1)
  GIGA_EDGE_UNPACK_ROUND_(>>, 3)

#undef GIGA_EDGE_UNPACK_ROUND_
}

#endif // using neon

// 8 -> 5 bytes.
void Pack5BitTailBlock(const std::uint8_t* GIGA_EDGE_RESTRICT_ src, std::uint8_t* GIGA_EDGE_RESTRICT_ dst) {
  constexpr auto significant_mask = DupExpandByte<std::uint32_t>(0b11111000);
  constexpr auto upper_mask = DupExpandByte<std::uint32_t>(0b11000000);

  auto first = ReadUnalignedInc<std::uint32_t>(src);
  auto second = ReadUnalignedInc<std::uint32_t>(src);
  auto mix = (first & significant_mask) | ((second >> 3) & ~significant_mask);
  auto pack = (second & upper_mask) % 1023; // base^i == 1 (mod base-1)
  WriteUnalignedInc(mix, dst);
  *dst = pack;
}

// 5 -> 8 bytes.
void Unpack5BitTailBlock(const std::uint8_t* GIGA_EDGE_RESTRICT_ src, std::uint8_t* GIGA_EDGE_RESTRICT_ dst) {
  constexpr auto significant_mask = DupExpandByte<std::uint32_t>(0b11111000);
  constexpr auto upper_mask = DupExpandByte<std::uint32_t>(0b11000000);
  constexpr std::uint32_t dup_k = ((1024 + 1) * 1024 + 1) * 1024 + 1;

  auto pack = ReadUnalignedInc<std::uint32_t>(src);
  std::uint32_t multi = (dup_k * *src) & upper_mask;
  WriteUnalignedInc(pack & significant_mask, dst);
  std::uint32_t acc = multi | ((pack & ~significant_mask) << 3);
  WriteUnalignedInc(acc, dst);
}

// By 8->5 blocks.
void Pack5BitTail(
  const std::uint8_t* GIGA_EDGE_RESTRICT_ src, std::size_t src_size, std::uint8_t* GIGA_EDGE_RESTRICT_ dst
) {
  for (; src_size >= unpacked_tail_block_size; src_size -= unpacked_tail_block_size) {
    Pack5BitTailBlock(src, dst);
    src += unpacked_tail_block_size;
    dst += packed_tail_block_size;
  }

  if (src_size) {
    std::uint8_t buf[unpacked_tail_block_size] = {};
    std::memcpy(buf, src, src_size);
    Pack5BitTailBlock(buf, dst);
  }
}

template<auto block_func>
void Unpack5BitTail(
  const std::uint8_t* GIGA_EDGE_RESTRICT_ src,
  std::size_t src_size,
  std::uint8_t* GIGA_EDGE_RESTRICT_ dst,
  std::size_t dst_size
) {
  assert((src_size * 8) >= (5 * dst_size));
  assert(0 == (src_size % 5));
  [[maybe_unused]] auto src_end = src + src_size;

  for (; dst_size >= unpacked_tail_block_size; dst_size -= unpacked_tail_block_size) {
    block_func(src, dst);
    src += packed_tail_block_size;
    dst += unpacked_tail_block_size;
  }

  if (dst_size) {
    std::uint8_t buf[8];
    assert(std::distance(src, src_end) >= packed_tail_block_size);
    block_func(src, buf);
    std::memcpy(dst, buf, dst_size);
  }
}

std::vector<std::uint8_t> Pack5Bit(const std::uint8_t* GIGA_EDGE_RESTRICT_ src, std::size_t size) {
  std::vector<std::uint8_t> out(((size * 5 + 39) / 40) * 5); // align size to 5 bytes
  std::uint8_t* dst = out.data();
  for (; size >= unpacked_block_size; size -= unpacked_block_size) {
    assert(std::distance(dst, out.data() + out.size()) >= packed_block_size);
    Pack5BitMainBlock(src, dst);
    src += unpacked_block_size;
    dst += packed_block_size;
  }
  Pack5BitTail(src, size, dst);
  return out;
}

void Unpack5Bit(
  const std::uint8_t* GIGA_EDGE_RESTRICT_ src,
  std::size_t src_size,
  std::uint8_t* GIGA_EDGE_RESTRICT_ dst,
  std::size_t dst_size
) {
  static_assert((packed_block_size % packed_tail_block_size) == 0);
  assert(src_size * 8 >= 5 * dst_size);
  assert(0 == (src_size % 5));

  auto const src_end = src + src_size;
  for (; dst_size >= unpacked_block_size; dst_size -= unpacked_block_size) {
#if GIGA_EDGE_PACKING_USING_ARM_NEON
    Unpack5BitMainBlockNeon(reinterpret_cast<const uint8x16_t*>(src), reinterpret_cast<uint8x16_t*>(dst));
#else
    Unpack5BitMainBlock(src, dst);
#endif
    src += packed_block_size;
    dst += unpacked_block_size;
  }
  Unpack5BitTail<Unpack5BitTailBlock>(src, src_end - src, dst, dst_size);
}

} // namespace

std::vector<std::uint8_t> Pack5BitData(const std::uint8_t* data, std::size_t size) {
  static_assert(sizeof(LaneVector) == 16);
  static_assert(packed_block_size == 80);
  return Pack5Bit(data, size);
}

bool Unpack5BitData(const std::uint8_t* src, std::size_t src_size, std::uint8_t* dst, std::size_t dst_size) {
  static_assert(sizeof(LaneVector) == 16);
  static_assert(packed_block_size == 80);
  static_assert((packed_block_size % packed_tail_block_size) == 0);

  if (((src_size % 5) != 0) || ((src_size * 8) < (5 * dst_size))) {
    return false;
  }

  Unpack5Bit(src, src_size, dst, dst_size);
  return true;
}

} // namespace giga_edge::packing
