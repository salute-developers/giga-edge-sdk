#include "memory_mapped_file.h"

#include "giga_edge/lib/data_reader/helpers.h"

#include <catch.hpp>

TEST_CASE("memory mapped file maps whole file content", "[util][data_reader]") {
  const giga_edge::data_reader::MappedFile mapped("data/test/test1.txt");
  auto span = giga_edge::EnsureNotNull(mapped.Map(0, 6));
  REQUIRE(span->View() == "Test 1");
}
