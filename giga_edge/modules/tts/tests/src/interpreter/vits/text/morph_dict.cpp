#include "giga_edge/modules/tts/interpreter/backends/vits/text/morph_dict.h"

#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/normalizer.h"

#include <catch.hpp>

#include <cstring>
#include <string>
#include <vector>

using namespace giga_edge;
using namespace giga_edge::vits;

namespace {

constexpr const char* kDataRoot = "data/vits";
// Built from source.xml beside it, made up in the format of the OpenCorpora export.
constexpr const char* kTestDirectory = "text/morph_test";

std::string TestFile() {
  const auto span = CreateDataReader(kDataRoot)->ReadPersistentData(std::string(kTestDirectory) + "/morph.dict");
  REQUIRE(span);
  return std::string(span->View());
}

MorphDict Load(const std::string& bytes, bool verify = true) {
  return MorphDict(
    data_reader::Create<IMovableDataSpanPtr>(std::vector<char>(bytes.begin(), bytes.end())), "test", verify
  );
}

void PutU32(std::string& bytes, size_t at, uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    bytes[at + i] = static_cast<char>((value >> (8 * i)) & 0xFF);
  }
}

} // namespace

TEST_CASE("vits morph dictionary: analyses of a word", "[tts][vits][morph]") {
  const auto dictionary = Load(TestFile());
  CHECK(dictionary.Size() > 0);
  const auto patient = dictionary.Find("пациента");
  REQUIRE(patient.Size() == 2);
  std::vector<GrammaticalCase> cases;
  for (size_t i = 0; i < patient.Size(); ++i) {
    CHECK(patient[i].part == PartOfSpeech::NOUN);
    CHECK(patient[i].reading.gender == Gender::MASCULINE);
    CHECK_FALSE(patient[i].reading.plural);
    CHECK(patient[i].reading.animate == (patient[i].reading.grammatical_case == GrammaticalCase::ACCUSATIVE));
    cases.push_back(patient[i].reading.grammatical_case);
  }
  CHECK(cases == std::vector<GrammaticalCase>{GrammaticalCase::GENITIVE, GrammaticalCase::ACCUSATIVE});
  CHECK_THROWS_AS(patient[2], std::out_of_range);
  // The animate accusative of a feminine is the genitive only in the plural.
  const auto student = dictionary.Find("студентку");
  REQUIRE(student.Size() == 1);
  CHECK_FALSE(student[0].reading.animate);
  bool animate_plural = false;
  const auto students = dictionary.Find("студенток");
  for (size_t i = 0; i < students.Size(); ++i) {
    animate_plural = animate_plural || (students[i].reading.animate && students[i].reading.plural);
  }
  CHECK(animate_plural);
  // Verbs and their transitivity; a short participle is an adjective.
  REQUIRE(dictionary.Find("видит").Size() == 1);
  CHECK(dictionary.Find("видит")[0].part == PartOfSpeech::VERB);
  CHECK(dictionary.Find("видит")[0].transitive);
  CHECK_FALSE(dictionary.Find("стоит")[0].transitive);
  CHECK(dictionary.Find("куплена")[0].part == PartOfSpeech::ADJECTIVE);
  // A word with a hyphen; all the words of the file.
  CHECK(dictionary.Find("интернет-магазин").Size() == 1);
  CHECK(dictionary.Size() == 51);
  // The gender, an indeclinable noun, a noun without a singular.
  REQUIRE(dictionary.Find("озеро").Size() == 1);
  CHECK(dictionary.Find("озеро")[0].reading.gender == Gender::NEUTER);
  CHECK(dictionary.Find("кофе")[0].indeclinable);
  CHECK_FALSE(dictionary.Find("озеро")[0].indeclinable);
  CHECK(dictionary.Find("ножны")[0].plural_only);
  CHECK_FALSE(dictionary.Find("озеро")[0].plural_only);
  // 'ё' and 'е' alike; unknown words, names, other letters, a word too long.
  CHECK(dictionary.Find("ёлку").Size() == 1);
  CHECK(dictionary.Find("елку").Size() == 1);
  CHECK(dictionary.Find("пациентт").Size() == 0);
  CHECK(dictionary.Find("пациен").Size() == 0);
  CHECK(dictionary.Find("а").Size() == 0);
  CHECK(dictionary.Find("яяя").Size() == 0);
  CHECK(dictionary.Find("иван").Size() == 0);
  CHECK(dictionary.Find("patient").Size() == 0);
  CHECK(dictionary.Find("").Size() == 0);
  CHECK(dictionary.Find(std::string(2 * (MorphDict::kMaxLetters + 1), 'a')).Size() == 0);
  std::string long_word;
  for (size_t i = 0; i <= MorphDict::kMaxLetters; ++i) {
    long_word += "я";
  }
  CHECK(dictionary.Find(long_word).Size() == 0);
}

TEST_CASE("vits morph dictionary: a damaged file", "[tts][vits][morph]") {
  const auto good = TestFile();
  CHECK_THROWS_WITH(Load(good.substr(0, 27)), Catch::Contains("'test'") && Catch::Contains("truncated: 27 bytes"));
  auto bytes = good;
  bytes[0] = 'X';
  CHECK_THROWS_WITH(Load(bytes), Catch::Contains("not a morph dictionary"));
  bytes = good;
  PutU32(bytes, 4, 2);
  CHECK_THROWS_WITH(Load(bytes), Catch::Contains("version 2"));
  CHECK_THROWS_WITH(Load(good.substr(0, good.size() - 1)), Catch::Contains("truncated or damaged"));
  CHECK_THROWS_WITH(Load(good + "x"), Catch::Contains("truncated or damaged"));
  bytes = good;
  PutU32(bytes, 8, 1000); // words that the blocks can not hold
  CHECK_THROWS_WITH(Load(bytes), Catch::Contains("header is damaged"));
  bytes = good;
  PutU32(bytes, 16, 0); // no words in a block
  CHECK_THROWS_WITH(Load(bytes), Catch::Contains("header is damaged"));

  // A damaged byte: the checksum tells; unchecked, a search stays inside the file.
  constexpr size_t header_size = 24;
  for (size_t at = header_size; at + 4 < good.size(); ++at) {
    for (const char value : {'\x00', '\x7F', '\xFF'}) {
      if (good[at] == value) {
        continue;
      }
      bytes = good;
      bytes[at] = value;
      INFO(at);
      CHECK_THROWS_WITH(Load(bytes), Catch::Contains("wrong checksum"));
      const auto unchecked = Load(bytes, false);
      for (const auto* word : {"пациента", "машину", "видит", "ёлках", "яяя", "а"}) {
        const auto analyses = unchecked.Find(word);
        for (size_t i = 0; i < analyses.Size(); ++i) {
          (void)analyses[i];
        }
      }
    }
  }
}

TEST_CASE("vits morph dictionary: the checksum of a large file", "[tts][vits][morph]") {
  // 10000 bytes of 0xFF overflow 32-bit sums unless they are reduced every 5552 bytes.
  std::string bytes = "GEMD" + std::string(20, '\0') + std::string(4, '\0') + std::string(10000, '\xFF');
  PutU32(bytes, 4, 1);
  PutU32(bytes, 8, 1);
  PutU32(bytes, 12, 1);
  PutU32(bytes, 16, 16);
  PutU32(bytes, 20, 10000);
  uint64_t low = 1;
  uint64_t high = 0;
  for (const char byte : bytes) {
    low = (low + static_cast<unsigned char>(byte)) % 65521;
    high = (high + low) % 65521;
  }
  bytes += std::string(4, '\0');
  PutU32(bytes, bytes.size() - 4, static_cast<uint32_t>((high << 16) | low));
  CHECK_NOTHROW(Load(bytes));
}

TEST_CASE("vits morph dictionary: a damaged record stays inside its block", "[tts][vits][morph]") {
  // One word in one block, the checksum not read.
  const auto file =
    [](const std::string& data, const std::string& checksum = std::string(4, '\0'), uint32_t words = 1) {
      std::string bytes = "GEMD" + std::string(20, '\0') + std::string(4, '\0') + data + checksum;
      PutU32(bytes, 4, 1);
      PutU32(bytes, 8, words);
      PutU32(bytes, 12, 1);
      PutU32(bytes, 16, 16);
      PutU32(bytes, 20, static_cast<uint32_t>(data.size()));
      return Load(bytes, false);
    };
  constexpr char a_code = 2;            // the code of 'а'
  const std::string tag("\x00\x00", 2); // a noun without a case
  CHECK(file(std::string{0, 1, a_code, 1} + tag).Find("а").Size() == 1);
  CHECK(file(std::string{0, 1, a_code, 2} + tag).Find("а").Size() == 0); // more analyses than bytes
  CHECK(file(std::string{0, 9, a_code, 1} + tag).Find("а").Size() == 0); // more letters than bytes
  // The second word has more letters than the block holds, the bytes after it would make "ааа" with one analysis.
  const std::string first = std::string{0, 1, a_code, 1} + tag;
  CHECK(file(first + std::string{0, 3, a_code, a_code}, std::string{a_code, 1, 0, 0}, 2).Find("ааа").Size() == 0);
  CHECK(
    file(first + std::string{0, 3, a_code, a_code, a_code, 1, 0, 0}, std::string(4, '\0'), 2).Find("ааа").Size() == 1
  );
  // A tag out of its ranges reads as no analysis.
  // A word of the longest length is found.
  CHECK(
    file(
      std::string{0, static_cast<char>(MorphDict::kMaxLetters)} + std::string(MorphDict::kMaxLetters, a_code)
      + std::string{1} + tag
    )
      .Find([] {
        std::string word;
        for (size_t i = 0; i < MorphDict::kMaxLetters; ++i) {
          word += "а";
        }
        return word;
      }())
      .Size()
    == 1
  );
  for (const auto* bad : {"\x1C\x00", "\x84\x01", "\x04\x20", "\x04\x40", "\x04\x80", "\x03\x00"}) {
    const auto dictionary = file(std::string{0, 1, a_code, 1} + std::string(bad, 2));
    const auto analyses = dictionary.Find("а");
    REQUIRE(analyses.Size() == 1);
    CHECK(analyses[0].part == PartOfSpeech::OTHER);
  }
}

TEST_CASE("vits normalizer with the morph dictionary", "[tts][vits][morph]") {
  const auto reader = CreateDataReader(kDataRoot);
  const TextNormalizer with(*reader, "text/ru", {}, nullptr, nullptr, LoadMorphWordInfo(*reader, kTestDirectory, true));
  const TextNormalizer without(*reader, "text/ru");
  CHECK_FALSE(LoadMorphWordInfo(*reader, "text/ru", true)); // no morph.dict there
  const auto read = [](const TextNormalizer& normalizer, std::string_view text) {
    return normalizer.Normalize(text).front().text;
  };
  struct Case {
    const char* text;
    const char* with;
    const char* without;
  };
  const Case cases[] = {
    // A transitive verb by the dictionary; without it "-ит" is not a verb, "-л" is.
    {"Видит 1 машину.", "видит одн+у машину.", "видит одн+у машину."},
    {"Видит 21 пациента.", "видит дв+адцать одног+о пациента.", "видит дв+адцать одн+а пациента."},
    {"Видел 21 пациента.", "видел дв+адцать одног+о пациента.", "видел дв+адцать одног+о пациента."},
    // An intransitive verb and the subject after it; a known noun outweighs the verb.
    {"Стоит 1 машина.", "стоит одн+а машина.", "стоит одн+а машина."},
    {"Стоял 21 мальчика.", "стоял дв+адцать одн+а мальчика.", "стоял дв+адцать одног+о мальчика."},
    // A noun of the dictionary: its animate accusative; a word it knows not as a noun has no readings.
    {"Вижу 2 студенток.", "вижу двух студенток.", "вижу двух студенток."},
    {"Вижу 21 пациента.", "вижу дв+адцать одног+о пациента.", "вижу дв+адцать одн+а пациента."},
    {"Будет 2 видит.", "будет два видит.", "будет два видит."},
    // A noun of the dictionary without a singular is counted as such.
    {"Купи 1 ножны.", "купи одн+и ножны.", "купи одн+ой ножны."},
    // A number after an abbreviated noun names it, a known noun after it does not make it an ordinal.
    {"В т. 6 машины.", "в т+оме шесть машины.", "в т+оме шесть машины."},
    // A noun of the dictionary that is also an adjective is the noun when no noun follows.
    {"Видел 3 вожатых у ворот.", "видел трёх вожатых у ворот.", "видел три вожатых у ворот."},
  };
  for (const auto& test_case : cases) {
    INFO(test_case.text);
    CHECK(read(with, test_case.text) == test_case.with);
    CHECK(read(without, test_case.text) == test_case.without);
  }
}
