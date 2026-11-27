#include "giga_edge/modules/fst/fst_rule.h"

#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/data_reader/helpers.h"

#include <catch.hpp>

#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

giga_edge::FstRulePtr CreateFstRule(const std::string& path) {
  return giga_edge::ReadFstRule(giga_edge::CreateDataReader("data/"), path);
}

const std::vector<std::string> kTexts{
  "один",
  "один два три",
  "вектор",
  "три вектор",
  "абракадабра",
  "",
};

// A directory reader that reads nothing itself: a rule is created from it only by mapping the file.
class PathOnlyReader : public giga_edge::IDataReader {
public:
  giga_edge::IMovableDataSpanPtr ReadTransientData(const std::string& path) override {
    throw std::runtime_error("unexpected read of " + path);
  }

  giga_edge::IDataSpanPtr ReadPersistentData(const std::string& path) override {
    throw std::runtime_error("unexpected read of " + path);
  }

  std::optional<std::string> GetPathPrefix() const override {
    return "data/";
  }
};

// An archive on disk that reads nothing itself: it only tells where a member is located in it.
class LocationOnlyReader : public giga_edge::IDataReader {
public:
  explicit LocationOnlyReader(giga_edge::IDataReaderPtr archive)
    : archive_(std::move(archive)) {}

  giga_edge::IMovableDataSpanPtr ReadTransientData(const std::string& path) override {
    throw std::runtime_error("unexpected read of " + path);
  }

  giga_edge::IDataSpanPtr ReadPersistentData(const std::string& path) override {
    throw std::runtime_error("unexpected read of " + path);
  }

  std::optional<giga_edge::ArchiveMemberLocation> GetArchiveMemberLocation(const std::string& path) const override {
    return archive_->GetArchiveMemberLocation(path);
  }

private:
  giga_edge::IDataReaderPtr archive_;
};

std::vector<char> ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("Vector fst", "[fst]") {
  auto fst = CreateFstRule("float_grammar/float_grammar.fst");
  std::unordered_map<std::string, std::string> tests = {
    {"один", "1"},
    {"один два три", "1 2 3"},
    {"вектор", "7.49 8.111"},
    {"три вектор", "3 7.49 8.111"},
    {"абракадабра", ""},
  };

  for (const auto& [text, expected] : tests) {
    INFO(text);
    CHECK(fst->Apply(text) == expected);
  }
}

// An aligned const FST (fstconvert --fst_type=const --fst_align) gives the same answers as the source vector FST.
TEST_CASE("Const fst answers as the vector one wherever it is read from", "[fst]") {
  const auto vector_rule = CreateFstRule("float_grammar/float_grammar.fst");
  const auto archive = ReadFile("data/float_grammar/float_grammar_const.tar");

  SECTION("a file of a directory, mapped") {
    const auto rule = CreateFstRule("float_grammar/float_grammar_const.fst");
    for (const auto& text : kTexts) {
      INFO(text);
      CHECK(rule->Apply(text) == vector_rule->Apply(text));
    }
  }
  SECTION("a member of an archive on disk, mapped") {
    const auto rule = giga_edge::ReadFstRule(
      giga_edge::CreateDataReader("data/float_grammar/float_grammar_const.tar"), "float_grammar_const.fst"
    );
    for (const auto& text : kTexts) {
      INFO(text);
      CHECK(rule->Apply(text) == vector_rule->Apply(text));
    }
  }
  SECTION("an archive in memory, read through the reader") {
    const auto rule = giga_edge::ReadFstRule(
      giga_edge::CreateTemporaryTarBufferDataReader(archive.data(), archive.size()), "float_grammar_const.fst"
    );
    for (const auto& text : kTexts) {
      INFO(text);
      CHECK(rule->Apply(text) == vector_rule->Apply(text));
    }
  }
}

TEST_CASE("Not an fst is rejected", "[fst]") {
  const std::string junk = "not an fst at all";
  REQUIRE_THROWS(
    giga_edge::ReadFstRule(giga_edge::CreateAnyPathSameSpanTemporaryDataReader(junk.data(), junk.size()), "x")
  );
  REQUIRE_THROWS(giga_edge::ReadFstRule(giga_edge::CreateAnyPathSameSpanTemporaryDataReader(nullptr, 0), "x"));
}

TEST_CASE("Const fst file is mapped without reading through the reader", "[fst]") {
  PathOnlyReader reader;
  const auto rule = giga_edge::ReadFstRule(reader, "float_grammar/float_grammar_const.fst");
  const auto vector_rule = CreateFstRule("float_grammar/float_grammar.fst");
  for (const auto& text : kTexts) {
    INFO(text);
    CHECK(rule->Apply(text) == vector_rule->Apply(text));
  }
}

TEST_CASE("Vector fst file is read through the reader", "[fst]") {
  PathOnlyReader reader;
  REQUIRE_THROWS_WITH(
    giga_edge::ReadFstRule(reader, "float_grammar/float_grammar.fst"),
    "unexpected read of float_grammar/float_grammar.fst"
  );
  LocationOnlyReader archive(giga_edge::CreateDataReader("data/float_grammar/float_grammar.tar"));
  REQUIRE_THROWS_WITH(giga_edge::ReadFstRule(archive, "float_grammar.fst"), "unexpected read of float_grammar.fst");
  const auto rule = giga_edge::ReadFstRule(
    giga_edge::CreateDataReader("data/float_grammar/float_grammar.tar"), "float_grammar.fst"
  );
  const auto vector_rule = CreateFstRule("float_grammar/float_grammar.fst");
  for (const auto& text : kTexts) {
    INFO(text);
    CHECK(rule->Apply(text) == vector_rule->Apply(text));
  }
}

TEST_CASE("Const fst member of an archive on disk is mapped without reading through the reader", "[fst]") {
  LocationOnlyReader reader(giga_edge::CreateDataReader("data/float_grammar/float_grammar_const.tar"));
  const auto rule = giga_edge::ReadFstRule(reader, "float_grammar_const.fst");
  const auto vector_rule = CreateFstRule("float_grammar/float_grammar.fst");
  for (const auto& text : kTexts) {
    INFO(text);
    CHECK(rule->Apply(text) == vector_rule->Apply(text));
  }
}

TEST_CASE("Truncated const fst is rejected wherever it is read from", "[fst]") {
  REQUIRE_THROWS(CreateFstRule("float_grammar/float_grammar_const_truncated.fst"));
  REQUIRE_THROWS(
    giga_edge::ReadFstRule(
      giga_edge::CreateDataReader("data/float_grammar/float_grammar_const_truncated.tar"),
      "float_grammar_const_truncated.fst"
    )
  );
  // The header promises more than the file holds: the mapping is refused, and without the reader there is no rule.
  PathOnlyReader reader;
  REQUIRE_THROWS(giga_edge::ReadFstRule(reader, "float_grammar/float_grammar_const_truncated.fst"));
}

TEST_CASE("Not an fst file in a directory is rejected", "[fst]") {
  REQUIRE_THROWS(CreateFstRule("not_fst.fst"));
}
