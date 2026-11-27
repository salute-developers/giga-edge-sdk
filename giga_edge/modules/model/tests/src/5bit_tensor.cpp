#include "giga_edge/lib/packing/5bit.h"
#include "giga_edge/modules/model/interpreter/interpreter.h"

#include <catch.hpp>

#include <cstring>
#include <random>

void TestPackingUnpacking(std::size_t source_size) {
  std::vector<std::uint8_t> source(source_size);
  std::mt19937 gen(source.size());
  for (auto& v : source) {
    v = gen() & 0b1111'1000;
  }

  auto packed = giga_edge::packing::Pack5BitData(source.data(), source.size());
  REQUIRE((packed.size() % 5) == 0);
  REQUIRE((packed.size() * 8) <= (5 * (source.size() + 7)));

  std::vector<std::uint8_t> unpacked(source.size());
  REQUIRE(giga_edge::packing::Unpack5BitData(packed.data(), packed.size(), unpacked.data(), unpacked.size()));
  REQUIRE(source == unpacked);
  unpacked.resize(unpacked.size() + 8);
  REQUIRE_FALSE(giga_edge::packing::Unpack5BitData(packed.data(), packed.size(), unpacked.data(), unpacked.size()));
}

TEST_CASE("test 5bit packing/unpacking", "[util][5bit]") {
  auto base_size = 1 + 313 * GENERATE(range(0, 5));
  for (auto add_size = 0; add_size < 256; ++add_size) {
    TestPackingUnpacking(base_size + add_size);
  }
}
