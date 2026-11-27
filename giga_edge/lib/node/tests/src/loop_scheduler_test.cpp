#include "giga_edge/lib/node/loop_scheduler.h"

#include "giga_edge/lib/common/binary_io.h"

#include <catch.hpp>

#include <deque>
#include <sstream>

using namespace giga_edge;

TEST_CASE("LoopRecorder accumulates iterations per node/seq into episodes", "[runtime][loop_scheduler]") {
  LoopRecorder rec;

  rec.OnIteration("a", 5);
  rec.OnIteration("a", 5);
  rec.OnIteration("a", 5);

  rec.OnIteration("a", 9);
  rec.OnIteration("a", 9);

  rec.OnIteration("b", 0);

  const auto& trace = rec.Trace();
  REQUIRE(trace.size() == 2);
  REQUIRE(trace.at("a") == std::deque<LoopEpisode>{{5, 3}, {9, 2}});
  REQUIRE(trace.at("b") == std::deque<LoopEpisode>{{0, 1}});
}

TEST_CASE("LoopRecorder::DecideInterrupt mirrors the live signal", "[runtime][loop_scheduler]") {
  LoopRecorder rec;
  REQUIRE(rec.DecideInterrupt("n", 0, false) == false);
  REQUIRE(rec.DecideInterrupt("n", 0, true) == true);
}

TEST_CASE("LoopTrace round-trips via binary_io", "[loop_scheduler]") {
  LoopTrace trace = {
    {"asr", {{5, 7}, {12, 3}}},
    {"vad node", {{9, 2}}},
  };

  std::ostringstream os;
  WriteBinary(&os, trace);

  std::istringstream is(os.str());
  REQUIRE(ReadBinary<LoopTrace>(&is) == trace);
}

TEST_CASE("LoopPlayer reproduces the recorded iteration count, ignoring the live signal", "[loop_scheduler]") {
  LoopPlayer player(LoopTrace{{"n", {{1, 5}}}});

  for (uint32_t i = 0; i < 5; ++i) {
    REQUIRE(player.DecideInterrupt("n", 1, true) == false);
    player.OnIteration("n", 1);
  }

  REQUIRE(player.DecideInterrupt("n", 1, false) == true);
  REQUIRE(player.Divergences().empty());
}

TEST_CASE("LoopPlayer interrupts immediately when nothing is recorded for the node", "[loop_scheduler]") {
  LoopPlayer player(LoopTrace{});
  REQUIRE(player.DecideInterrupt("n", 1, false) == true);
}

TEST_CASE("LoopPlayer flags an unexpected iteration when nothing is recorded", "[loop_scheduler]") {
  LoopPlayer player(LoopTrace{});

  player.OnIteration("n", 1);

  REQUIRE(player.Divergences().size() == 1);
  REQUIRE(player.Divergences()[0].find("no recorded episode") != std::string::npos);
}

TEST_CASE("LoopPlayer flags a seq mismatch against the recorded episode", "[loop_scheduler]") {
  LoopPlayer player(LoopTrace{{"n", {{1, 2}}}});

  player.OnIteration("n", 99);

  REQUIRE(player.Divergences().size() == 1);
  REQUIRE(player.Divergences()[0].find("seq mismatch") != std::string::npos);
}
