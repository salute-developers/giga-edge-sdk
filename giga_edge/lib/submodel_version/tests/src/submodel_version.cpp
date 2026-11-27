#include "giga_edge/lib/submodel_version/submodel_version.h"

#include <catch.hpp>

#include <set>
#include <vector>

TEST_CASE("Test SubmodelVersion", "[submodel_version]") {
  giga_edge::SubmodelVersionChain test_chain{
    .ids_chain = {
      {.id = "id1", .version = "version1"},
      {.id = "id2", .version = "version2"},
      {.id = "id3", .version = "version3"},
      {.id = "id4", .version = "version4"},
      {.id = "id5", .version = "version5"},
    }
  };

  REQUIRE(
    test_chain.PopBack()
    == giga_edge::SubmodelVersionChain{
      .ids_chain = {
        {.id = "id1", .version = "version1"},
        {.id = "id2", .version = "version2"},
        {.id = "id3", .version = "version3"},
        {.id = "id4", .version = "version4"},
      }
    }
  );

  REQUIRE(
    test_chain.PopBack().PopBack()
    == giga_edge::SubmodelVersionChain{
      .ids_chain = {
        {.id = "id1", .version = "version1"},
        {.id = "id2", .version = "version2"},
        {.id = "id3", .version = "version3"},
      }
    }
  );

  REQUIRE(
    test_chain.PopFront()
    == giga_edge::SubmodelVersionChain{
      .ids_chain = {
        {.id = "id2", .version = "version2"},
        {.id = "id3", .version = "version3"},
        {.id = "id4", .version = "version4"},
        {.id = "id5", .version = "version5"},
      }
    }
  );

  REQUIRE(
    test_chain.PopFront().PopFront()
    == giga_edge::SubmodelVersionChain{
      .ids_chain = {
        {.id = "id3", .version = "version3"},
        {.id = "id4", .version = "version4"},
        {.id = "id5", .version = "version5"},
      }
    }
  );

  REQUIRE(test_chain.AsString() == "id1#version1#id2#version2#id3#version3#id4#version4#id5#version5");

  REQUIRE(
    giga_edge::SubmodelVersionChain{
      .ids_chain = {
        {.id = "id1", .version = "version1"},
        {.id = "id2", .version = "version2"},
      },
    }.IsPrefixOf(test_chain)
  );

  REQUIRE(
    !giga_edge::SubmodelVersionChain{
      .ids_chain = {
        {.id = "id1", .version = "version1"},
        {.id = "id3", .version = "version3"},
      },
    }.IsPrefixOf(test_chain)
  );

  REQUIRE(
    giga_edge::SubmodelVersionChain{
      .ids_chain = {},
    }
      .IsPrefixOf(test_chain)
  );

  REQUIRE(test_chain == test_chain);
  REQUIRE(!(test_chain == test_chain.PopBack()));
  REQUIRE(!(test_chain == test_chain.PopFront()));
}
