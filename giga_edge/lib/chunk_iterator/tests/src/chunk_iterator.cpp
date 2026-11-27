#include "giga_edge/lib/chunk_iterator/chunk_iterator.h"

#include "giga_edge/lib/config_parser/config_parser.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Chunk iterator, step size is the same as chunk size", "[chunk_iterator]") {
  ChunkIterator<float> iterator({.chunk_size = 3, .step_size = 3});

  REQUIRE(!iterator.HasNext());
  iterator.Push({1.0, 2.0, 3.0, 4.0, 5.0});

  REQUIRE(iterator.HasNext());
  REQUIRE(iterator.Next() == std::vector<float>{1.0, 2.0, 3.0});
  REQUIRE(!iterator.HasNext());
  iterator.Push({6.0, 7.0});

  REQUIRE(iterator.Next() == std::vector<float>{4.0, 5.0, 6.0});
  REQUIRE(!iterator.HasNext());
}

TEST_CASE("Chunk iterator, step size differs from chunk size", "[chunk_iterator]") {
  ChunkIterator<float> iterator({.chunk_size = 3, .step_size = 2});

  REQUIRE(!iterator.HasNext());
  iterator.Push({1.0, 2.0, 3.0, 4.0, 5.0});

  REQUIRE(iterator.HasNext());
  REQUIRE(iterator.Next() == std::vector<float>{1.0, 2.0, 3.0});

  REQUIRE(iterator.HasNext());
  REQUIRE(iterator.Next() == std::vector<float>{3.0, 4.0, 5.0});

  REQUIRE(!iterator.HasNext());

  iterator.Push({6.0, 7.0});

  REQUIRE(iterator.Next() == std::vector<float>{5.0, 6.0, 7.0});
  REQUIRE(!iterator.HasNext());
}

TEST_CASE("Chunk iterator, reset", "[chunk_iterator]") {
  ChunkIterator<float> iterator({.chunk_size = 3, .step_size = 3});

  REQUIRE(!iterator.HasNext());

  iterator.Push({1.0, 2.0, 3.0, 4.0, 5.0});
  REQUIRE(iterator.HasNext());

  iterator.Reset();
  REQUIRE(!iterator.HasNext());

  iterator.Push({6.0, 7.0, 8.0});
  REQUIRE(iterator.HasNext());
  REQUIRE(iterator.Next() == std::vector<float>{6.0, 7.0, 8.0});
}

namespace giga_edge {

inline bool operator==(const ChunkIteratorParams& lhs, const ChunkIteratorParams& rhs) {
  return std::tie(lhs.chunk_size, lhs.step_size) == std::tie(rhs.chunk_size, rhs.step_size);
}

} // namespace giga_edge

TEST_CASE("Chunk iterator, parsing params", "[chunk_iterator]") {
  REQUIRE(
    ParseConfig<ChunkIteratorParams>(YAML::Load("chunk_size: 10"))
    == ChunkIteratorParams{
      .chunk_size = 10,
      .step_size = 10,
    }
  );

  REQUIRE(
    ParseConfig<ChunkIteratorParams>(YAML::Load(
      R"(
        chunk_size: 10
        step_size: 3
      )"
    ))
    == ChunkIteratorParams{
      .chunk_size = 10,
      .step_size = 3,
    }
  );
}