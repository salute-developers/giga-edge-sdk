#include "giga_edge/lib/sequence_id_generator/sequence_id_generator.h"

#include <catch.hpp>

#include <unordered_set>

using namespace giga_edge;

TEST_CASE("Test sequence_id_generator, simple", "[sequence_id_generator]") {
  auto generator = SequenceIdGenerator({"input1", "input2", "input3"});
  REQUIRE(generator.GenerateNextInputId("input1") == 0);
  REQUIRE(generator.GenerateNextInputId("input1") == 1);
  REQUIRE(generator.GenerateNextInputId("input1") == 2);
  REQUIRE(generator.GenerateNextInputId("input2") == 0);
  REQUIRE(generator.GenerateNextInputId("input2") == 1);
  REQUIRE(generator.GenerateNextInputId("input2") == 2);
  REQUIRE(generator.GenerateNextInputId("input3") == 0);
  REQUIRE(generator.GenerateNextInputId("input3") == 1);
  REQUIRE(generator.GenerateNextInputId("input3") == 2);
  REQUIRE(generator.GenerateNextInputId("input1") == 3);
  REQUIRE(generator.NextSeqId() == 4);
}

TEST_CASE("Test sequence_id_generator, with_events", "[sequence_id_generator]") {
  auto generator = SequenceIdGenerator({"input1", "input2", "input3"});
  REQUIRE(generator.GenerateNextInputId("input1") == 0);
  REQUIRE(generator.GenerateNextInputId("input1") == 1);
  REQUIRE(generator.GenerateNextInputId("input1") == 2);
  REQUIRE(generator.GenerateNextEventId() == 3);
  REQUIRE(generator.GenerateNextEventId() == 4);
  REQUIRE(generator.GenerateNextEventId() == 5);
  REQUIRE(generator.GenerateNextInputId("input2") == 0);
  REQUIRE(generator.GenerateNextInputId("input2") == 1);
  REQUIRE(generator.GenerateNextInputId("input2") == 2);
  REQUIRE(generator.GenerateNextInputId("input3") == 0);
  REQUIRE(generator.GenerateNextInputId("input3") == 1);
  REQUIRE(generator.GenerateNextInputId("input3") == 2);
  REQUIRE(generator.GenerateNextInputId("input1") == 6);
  REQUIRE(generator.GenerateNextInputId("input1") == 7);
  REQUIRE(generator.GenerateNextInputId("input2") == 6);
  REQUIRE(generator.NextSeqId() == 8);
}