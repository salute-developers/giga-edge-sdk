#include "giga_edge/modules/tts/interpreter/backends/vits/text/stress_dict.h"

#include "stress_helpers.h"

#include "giga_edge/modules/tts/interpreter/backends/vits/text/table.h"

#include <catch.hpp>

#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace giga_edge;
using namespace giga_edge::vits;
using namespace giga_edge::vits::test;
using Catch::Matchers::Contains;

namespace {

// "зам+ок" -> "замок" and its marks, as the dictionary must answer.
std::pair<std::string, StressDict::Marks> Expected(const std::string& marked) {
  std::string word;
  StressDict::Marks marks;
  size_t letters = 0;
  for (size_t pos = 0; pos < marked.size();) {
    if (marked[pos] == '+') {
      marks.set(letters);
      pos += 1;
      continue;
    }
    const size_t size = marked[pos] == '-' ? 1 : 2;
    word += marked.substr(pos, size);
    pos += size;
    ++letters;
  }
  return {word, marks};
}

std::string Repeat(const std::string& text, size_t times) {
  std::string result;
  for (size_t i = 0; i < times; ++i) {
    result += text;
  }
  return result;
}

const std::vector<std::string> kWords = {
  "+а",   "+аист",  "а+ист-к+а", "аб+ажур", "е+ё",        "+ёж",      "+ёжик",
  "ж+ук", "жук+и",  "жук+ов",    "зам+ок",  "з+амки",     "замк+ом",  "и+юль",
  "+я",   "+я-т+о", "+ябеда",    "я+яя",    "по-р+усски", "трёхс+от", "тр+ёхсотл+етний",
};

} // namespace

TEST_CASE("vits word dictionary finds every word and only them", "[tts][vits][stress_dict]") {
  const auto block_words = GENERATE(1U, 2U, 3U, 16U, 100U);
  const StressDict dict(MakeSpan(BuildDict(kWords, block_words)), "test.dict");
  REQUIRE(dict.Size() == kWords.size());
  for (const auto& marked : kWords) {
    const auto [word, marks] = Expected(marked);
    CAPTURE(block_words, word);
    const auto found = dict.Find(word);
    REQUIRE(found.has_value());
    REQUIRE(*found == marks);
  }
  // Before the first word, after the last one, between the words, a prefix of a word and a word with a tail.
  for (const std::string absent :
       {"",     "-",      "-а",  "аи", "аис", "аисты", "аист-", "аист-к", "ёжики",    "жу",    "жука",
        "замо", "замоки", "ее-", "еж", "яя",  "яяяя",  "по",    "по-",    "по-русск", "русски"}) {
    CAPTURE(block_words, absent);
    REQUIRE_FALSE(dict.Find(absent).has_value());
  }
}

TEST_CASE("vits word dictionary tells 'е' from 'ё' and keeps several marks", "[tts][vits][stress_dict]") {
  const StressDict dict(MakeSpan(BuildDict(kWords, 4)), "test.dict");
  REQUIRE(dict.Find("её")->count() == 1);
  REQUIRE(dict.Find("её")->test(1));
  REQUIRE_FALSE(dict.Find("ежик").has_value());
  const auto several = dict.Find("трёхсотлетний");
  REQUIRE(several.has_value());
  REQUIRE(several->count() == 2);
  REQUIRE(several->test(2));
  REQUIRE(several->test(8));
  const auto hyphenated = dict.Find("аист-ка");
  REQUIRE(hyphenated.has_value());
  REQUIRE(hyphenated->test(1));
  REQUIRE(hyphenated->test(6));
}

TEST_CASE("vits word dictionary does not find what is not a Russian lower-case word", "[tts][vits][stress_dict]") {
  const StressDict dict(MakeSpan(BuildDict(kWords)), "test.dict");
  for (const std::string query :
       {"Жук",
        "жуK",
        "жук ",
        " жук",
        "жу+к",
        "жук1",
        "zhuk",
        "ж",
        "\xD0",
        "жу\xD0",
        "\xFF\xFF",
        "жук\n",
        "ｊ",
        // Two bytes that only look like "а" and "я" when the second one is not checked.
        "\xD0\x30",
        "\xD1\x0F",
        "жу\xD0\x3A"}) {
    CAPTURE(query);
    REQUIRE_FALSE(dict.Find(query).has_value());
  }
}

TEST_CASE("vits word dictionary holds words up to 127 letters", "[tts][vits][stress_dict]") {
  const auto longest = Repeat("а", StressDict::kMaxLetters - 1) + "+я";
  const StressDict dict(MakeSpan(BuildDict({longest, "+а"}, 1)), "test.dict");
  const auto found = dict.Find(Repeat("а", StressDict::kMaxLetters - 1) + "я");
  REQUIRE(found.has_value());
  REQUIRE(found->test(StressDict::kMaxLetters - 1));
  REQUIRE(found->count() == 1);
  REQUIRE_FALSE(dict.Find(Repeat("а", StressDict::kMaxLetters) + "я").has_value());
  REQUIRE_FALSE(dict.Find(Repeat("а", 1000)).has_value());
}

TEST_CASE("vits word dictionary may be empty", "[tts][vits][stress_dict]") {
  const StressDict dict(MakeSpan(BuildDict({})), "test.dict");
  REQUIRE(dict.Size() == 0);
  REQUIRE_FALSE(dict.Find("а").has_value());
  REQUIRE_FALSE(dict.Find("").has_value());
}

TEST_CASE("vits test word dictionary is read word by word", "[tts][vits][stress_dict]") {
  const auto reader = CreateDataReader("data/");
  const auto directory = std::string(kTextDirectory) + "/";
  const StressDict dict(reader->ReadPersistentData(directory + "stress.dict"), "stress.dict");
  const auto source = ToString(reader->ReadTransientData(directory + "stress_words.txt"));
  const auto rows = ReadTable(source);
  REQUIRE(rows.size() > 50);
  REQUIRE(dict.Size() == rows.size());
  for (const auto& row : rows) {
    const auto [word, marks] = Expected(std::string(row.fields[0]));
    CAPTURE(word);
    const auto found = dict.Find(word);
    REQUIRE(found.has_value());
    REQUIRE(*found == marks);
  }
  REQUIRE_FALSE(dict.Find("спотифаем").has_value());

  const StressDict yo(reader->ReadPersistentData(directory + "yo.dict"), "yo.dict");
  REQUIRE(yo.Size() == 10);
  REQUIRE(yo.Find("елка")->test(0));
  REQUIRE(yo.Find("ушел")->test(2));
  REQUIRE_FALSE(yo.Find("ёлка").has_value());
}

TEST_CASE("vits word dictionary reports a bad file at load", "[tts][vits][stress_dict]") {
  const auto good = BuildDict(kWords, 4);
  REQUIRE_NOTHROW(StressDict(MakeSpan(good), "test.dict"));

  SECTION("no data") {
    REQUIRE_THROWS_WITH(StressDict(nullptr, "test.dict"), Contains("word dictionary 'test.dict': no data"));
  }
  SECTION("truncated at any byte") {
    for (size_t size = 0; size < good.size(); ++size) {
      CAPTURE(size);
      REQUIRE_THROWS_WITH(
        StressDict(MakeSpan(good.substr(0, size)), "test.dict"),
        Contains("word dictionary 'test.dict': the file is truncated")
      );
      REQUIRE_THROWS_WITH(StressDict(MakeSpan(good.substr(0, size)), "test.dict", false), Contains("truncated"));
    }
  }
  SECTION("longer than the header says") {
    REQUIRE_THROWS_WITH(StressDict(MakeSpan(good + "x"), "test.dict"), Contains("truncated or damaged"));
  }
  SECTION("another format") {
    auto bad = good;
    bad[0] = 'X';
    REQUIRE_THROWS_WITH(StressDict(MakeSpan(bad), "test.dict"), Contains("'test.dict': not a word dictionary"));
    REQUIRE_THROWS_WITH(StressDict(MakeSpan(std::string(100, '\0')), "test.dict"), Contains("not a word dictionary"));
  }
  SECTION("another version") {
    auto bad = good;
    bad[4] = 2;
    REQUIRE_THROWS_WITH(
      StressDict(MakeSpan(bad), "test.dict"), Contains("'test.dict': unsupported version 2, expected 1")
    );
  }
  SECTION("a damaged byte anywhere after the version") {
    for (size_t pos = 8; pos < good.size(); ++pos) {
      auto bad = good;
      bad[pos] = static_cast<char>(bad[pos] ^ 0x21);
      CAPTURE(pos);
      REQUIRE_THROWS_WITH(StressDict(MakeSpan(bad), "test.dict"), Contains("word dictionary 'test.dict': the "));
    }
  }
  SECTION("a wrong checksum alone") {
    auto bad = good;
    bad.back() = static_cast<char>(bad.back() ^ 1);
    REQUIRE_THROWS_WITH(StressDict(MakeSpan(bad), "test.dict"), Contains("the file is damaged: wrong checksum"));
    REQUIRE_NOTHROW(StressDict(MakeSpan(bad), "test.dict", false));
  }
  SECTION("a header that contradicts itself") {
    auto bad = good;
    bad[16] = 0; // words in a block
    REQUIRE_THROWS_WITH(StressDict(MakeSpan(bad), "test.dict", false), Contains("the header is damaged"));
    bad = good;
    bad[8] = static_cast<char>(bad[8] ^ 0x40); // words: more than the blocks hold
    REQUIRE_THROWS_WITH(StressDict(MakeSpan(bad), "test.dict", false), Contains("the header is damaged"));
  }
}

// Without the checksum a damaged file is not reported; a search must stay inside it (the sanitizer build checks).
TEST_CASE("vits word dictionary search survives a damaged file", "[tts][vits][stress_dict]") {
  const auto good = BuildDict(kWords, 3);
  size_t answers = 0;
  for (size_t pos = 24; pos < good.size() - 4; ++pos) {
    for (const unsigned char value : {0x00, 0x01, 0x7F, 0x80, 0xFF}) {
      auto bad = good;
      bad[pos] = static_cast<char>(value);
      const StressDict dict(MakeSpan(bad), "test.dict", false);
      for (const auto& marked : kWords) {
        answers += dict.Find(Expected(marked).first).has_value() ? 1 : 0;
      }
      answers += dict.Find("яяяяяя").has_value() ? 1 : 0;
    }
  }
  REQUIRE(answers > 0);
}

TEST_CASE("vits word dictionary checks a long file and a header of too many blocks", "[tts][vits][stress_dict]") {
  // More bytes than the sums of the checksum bear without their reduction.
  std::vector<std::string> words;
  const std::string letters[] = {"б", "в", "г", "д", "ж", "з", "к", "л", "м", "н", "п", "р", "с", "т", "ф", "х"};
  for (const auto& first : letters) {
    for (const auto& second : letters) {
      for (const auto& third : letters) {
        for (const auto& fourth : letters) {
          words.push_back(first + "я" + second + "ю" + third + "+я" + fourth + "юя");
        }
      }
    }
  }
  const auto large = BuildDict(words);
  REQUIRE(large.size() > 300000);
  const StressDict dict(MakeSpan(large), "large.dict");
  REQUIRE(dict.Size() == words.size());
  REQUIRE(dict.Find("бябюбябюя")->test(5));

  // 2^30 more blocks in the header: four bytes an offset, the size wraps to the true one in 32 bits.
  auto damaged = BuildDict({"з+амок", "св+ет"}, 1);
  damaged[15] = static_cast<char>(damaged[15] | 0x40);
  REQUIRE_THROWS_WITH(StressDict(MakeSpan(damaged), "damaged.dict", false), Contains("truncated or damaged"));
}
