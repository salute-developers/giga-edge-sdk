#include "giga_edge/lib/common/binary_io.h"

#include <catch.hpp>

#include <deque>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

template<typename T>
std::vector<char> SerializeToBytes(const T& value) {
  std::ostringstream stream(std::ios::out | std::ios::binary);
  giga_edge::WriteBinary(&stream, value);
  const std::string bytes = stream.str();
  return std::vector<char>(bytes.begin(), bytes.end());
}

template<typename T>
T DeserializeFromBytes(const std::vector<char>& bytes) {
  std::istringstream stream(std::string(bytes.begin(), bytes.end()), std::ios::in | std::ios::binary);
  T decoded{};
  giga_edge::ReadBinary(&stream, decoded);
  return decoded;
}

} // namespace

TEST_CASE("binary_io roundtrip for scalar", "[common][binary_io]") {
  const int32_t expected = 1337;
  const auto bytes = SerializeToBytes(expected);
  const int32_t actual = DeserializeFromBytes<int32_t>(bytes);
  REQUIRE(actual == expected);
}

TEST_CASE("binary_io roundtrip for pair", "[common][binary_io]") {
  const std::pair<int32_t, float> expected{7, 3.5f};
  const auto bytes = SerializeToBytes(expected);
  const auto actual = DeserializeFromBytes<std::pair<int32_t, float>>(bytes);
  REQUIRE(actual == expected);
}

TEST_CASE("binary_io roundtrip for vector", "[common][binary_io]") {
  const std::vector<float> expected{1.0f, -2.5f, 3.25f};
  const auto bytes = SerializeToBytes(expected);
  const auto actual = DeserializeFromBytes<std::vector<float>>(bytes);
  REQUIRE(actual == expected);
}

TEST_CASE("binary_io roundtrip for 2d vector", "[common][binary_io]") {
  const std::vector<std::vector<float>> expected{
    {1.0f, 2.0f, 3.0f},
    {10.0f, 20.0f, 30.0f},
    {-1.5f, 0.0f, 1.5f},
  };
  const auto bytes = SerializeToBytes(expected);
  const auto actual = DeserializeFromBytes<std::vector<std::vector<float>>>(bytes);
  REQUIRE(actual == expected);
}

TEST_CASE("binary_io roundtrip for string", "[common][binary_io]") {
  const std::string expected = "node id with spaces";
  const auto bytes = SerializeToBytes(expected);
  const auto actual = DeserializeFromBytes<std::string>(bytes);
  REQUIRE(actual == expected);
}

TEST_CASE("binary_io roundtrip for empty string", "[common][binary_io]") {
  const std::string expected;
  const auto bytes = SerializeToBytes(expected);
  REQUIRE(DeserializeFromBytes<std::string>(bytes).empty());
}

TEST_CASE("binary_io roundtrip for string with embedded null", "[common][binary_io]") {
  const std::string expected("a\0b", 3);
  const auto bytes = SerializeToBytes(expected);
  const auto actual = DeserializeFromBytes<std::string>(bytes);
  REQUIRE(actual == expected);
  REQUIRE(actual.size() == 3);
}

TEST_CASE("binary_io roundtrip for deque", "[common][binary_io]") {
  const std::deque<int32_t> expected{1, -2, 3, 4};
  const auto bytes = SerializeToBytes(expected);
  const auto actual = DeserializeFromBytes<std::deque<int32_t>>(bytes);
  REQUIRE(actual == expected);
}

TEST_CASE("binary_io roundtrip for unordered_map", "[common][binary_io]") {
  const std::unordered_map<std::string, int32_t> expected{{"a", 1}, {"b", 2}, {"c", 3}};
  const auto bytes = SerializeToBytes(expected);
  const auto actual = DeserializeFromBytes<std::unordered_map<std::string, int32_t>>(bytes);
  REQUIRE(actual == expected);
}

TEST_CASE("binary_io roundtrip for map of deques", "[common][binary_io]") {
  const std::unordered_map<std::string, std::deque<int32_t>> expected{
    {"asr", {5, 7, 12}},
    {"vad", {9}},
  };
  const auto bytes = SerializeToBytes(expected);
  const auto actual = DeserializeFromBytes<std::unordered_map<std::string, std::deque<int32_t>>>(bytes);
  REQUIRE(actual == expected);
}

TEST_CASE("binary_io throws on truncated payload", "[common][binary_io]") {
  const std::vector<char> invalid_bytes{0x01, 0x02};
  REQUIRE_THROWS_AS(DeserializeFromBytes<int32_t>(invalid_bytes), std::runtime_error);
}
