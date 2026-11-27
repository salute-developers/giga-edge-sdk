#include "giga_edge/lib/common/syncronizer.h"

#include <catch.hpp>

#include <string>
#include <tuple>
#include <vector>

TEST_CASE("Syncronizer waits until all inputs are ready", "[common][syncronizer]") {
  std::vector<std::tuple<int, std::string>> calls;
  giga_edge::Syncronizer<int, std::string> syncronizer([&calls](int value, std::string text) {
    calls.emplace_back(value, std::move(text));
  });

  syncronizer.Push<0>(42);
  REQUIRE(calls.empty());

  syncronizer.Push<1>("ready");
  REQUIRE(calls == std::vector<std::tuple<int, std::string>>{{42, "ready"}});
}

TEST_CASE("Syncronizer preserves queue order", "[common][syncronizer]") {
  std::vector<std::tuple<int, std::string>> calls;
  giga_edge::Syncronizer<int, std::string> syncronizer([&calls](int value, std::string text) {
    calls.emplace_back(value, std::move(text));
  });

  syncronizer.Push<0>(1);
  syncronizer.Push<0>(2);
  syncronizer.Push<1>("one");
  syncronizer.Push<1>("two");

  REQUIRE(
    calls
    == std::vector<std::tuple<int, std::string>>{
      {1, "one"},
      {2, "two"},
    }
  );
}

TEST_CASE("Syncronizer drops queued values before start seq", "[common][syncronizer]") {
  std::vector<std::tuple<int, std::string>> calls;
  giga_edge::Syncronizer<int, std::string> syncronizer([&calls](int value, std::string text) {
    calls.emplace_back(value, std::move(text));
  });

  syncronizer.Push<0>(1, 10);
  syncronizer.Push<0>(2, 20);
  syncronizer.Start(2);
  REQUIRE(calls.empty());

  syncronizer.Push<1>(2, "twenty");
  REQUIRE(calls == std::vector<std::tuple<int, std::string>>{{20, "twenty"}});
}

TEST_CASE("Syncronizer ignores pushed values before started seq", "[common][syncronizer]") {
  std::vector<std::tuple<int, std::string>> calls;
  giga_edge::Syncronizer<int, std::string> syncronizer([&calls](int value, std::string text) {
    calls.emplace_back(value, std::move(text));
  });

  syncronizer.Start(10);
  syncronizer.Push<0>(9, 90);
  syncronizer.Push<1>(9, "old");
  REQUIRE(calls.empty());

  syncronizer.Push<0>(10, 100);
  syncronizer.Push<1>(10, "current");
  REQUIRE(calls == std::vector<std::tuple<int, std::string>>{{100, "current"}});
}
