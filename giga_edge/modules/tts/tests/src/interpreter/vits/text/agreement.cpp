#include "giga_edge/modules/tts/interpreter/backends/vits/text/agreement.h"

#include "giga_edge/lib/data_reader/data_reader.h"

#include <catch.hpp>

#include <string>
#include <vector>

using namespace giga_edge;
using namespace giga_edge::vits;

namespace {

constexpr auto kNom = GrammaticalCase::NOMINATIVE;
constexpr auto kGen = GrammaticalCase::GENITIVE;
constexpr auto kDat = GrammaticalCase::DATIVE;
constexpr auto kAcc = GrammaticalCase::ACCUSATIVE;
constexpr auto kIns = GrammaticalCase::INSTRUMENTAL;
constexpr auto kPre = GrammaticalCase::PREPOSITIONAL;
constexpr auto kMasc = Gender::MASCULINE;
constexpr auto kFem = Gender::FEMININE;
constexpr auto kNeut = Gender::NEUTER;

const Lexicon& TestLexicon() {
  static const Lexicon lexicon(*CreateDataReader("data/vits"), "text/ru");
  return lexicon;
}

std::vector<NounReading> Readings(const std::string& word) {
  const auto* match = TestLexicon().FindNoun(word);
  if (match != nullptr) {
    return match->readings;
  }
  return TestLexicon().IsNotNoun(word) ? std::vector<NounReading>() : GuessNounReadings(word);
}

std::string Read(uint64_t value, const std::string& left, const std::string& noun, const std::string& suffix = {}) {
  NumberContext context;
  context.governed = TestLexicon().GovernedCases(left);
  context.noun = Readings(noun);
  context.noun_guessed = TestLexicon().FindNoun(noun) == nullptr;
  if (const auto* match = TestLexicon().FindNoun(noun)) {
    context.ordinal_use = match->noun->ordinal_use;
  }
  context.suffix = suffix;
  return ReadNumber(value, Agree(value, context));
}

} // namespace

TEST_CASE("vits lexicon: a verb by its ending", "[tts][vits][agreement]") {
  CHECK(TestLexicon().LooksLikeVerb("принял"));
  CHECK(TestLexicon().LooksLikeVerb("купить"));
  CHECK_FALSE(TestLexicon().LooksLikeVerb("капитал"));
  CHECK_FALSE(TestLexicon().LooksLikeVerb("ил"));
  CHECK_FALSE(TestLexicon().LooksLikeVerb("стоит"));
  CHECK(TestLexicon().WordGovernedCases("достигает") == std::vector<GrammaticalCase>{kGen});
  CHECK(TestLexicon().WordGovernedCases("ждали").empty());
}

TEST_CASE("vits agreement: governing words", "[tts][vits][agreement]") {
  CHECK(TestLexicon().GovernedCases("к") == std::vector<GrammaticalCase>{kDat});
  CHECK(TestLexicon().GovernedCases("в") == std::vector<GrammaticalCase>{kAcc, kPre});
  CHECK(TestLexicon().GovernedCases("с") == std::vector<GrammaticalCase>{kGen, kIns});
  CHECK(TestLexicon().GovernedCases("по") == std::vector<GrammaticalCase>{kAcc, kDat, kPre});
  CHECK(TestLexicon().GovernedCases("между") == std::vector<GrammaticalCase>{kIns});
  CHECK(TestLexicon().GovernedCases("о") == std::vector<GrammaticalCase>{kPre, kAcc});
  CHECK(TestLexicon().GovernedCases("нет") == std::vector<GrammaticalCase>{kGen});
  CHECK(TestLexicon().GovernedCases("около") == std::vector<GrammaticalCase>{kGen});
  CHECK(TestLexicon().GovernedCases("через") == std::vector<GrammaticalCase>{kAcc});
  CHECK(TestLexicon().GovernedCases("дом").empty());
  CHECK(TestLexicon().GovernedCases("").empty());
}

TEST_CASE("vits agreement: noun forms by the ending", "[tts][vits][agreement]") {
  const auto has = [](const std::string& word, NounReading expected) {
    const auto readings = GuessNounReadings(word);
    for (const auto& reading : readings) {
      if (
        reading.gender == expected.gender && reading.plural == expected.plural
        && reading.grammatical_case == expected.grammatical_case
      ) {
        return true;
      }
    }
    return false;
  };
  CHECK(has("книгами", {kMasc, true, kIns}));
  CHECK(has("книгам", {kMasc, true, kDat}));
  CHECK(has("книгах", {kMasc, true, kPre}));
  CHECK(has("вопросов", {kMasc, true, kGen}));
  CHECK(has("минута", {kFem, false, kNom}));
  CHECK(has("ученика", {kMasc, false, kGen}));
  CHECK(has("письма", {kNeut, false, kGen}));
  CHECK(has("книги", {kFem, false, kGen}));
  CHECK(has("розу", {kFem, false, kAcc}));
  CHECK(has("яблоко", {kNeut, false, kNom}));
  CHECK(has("сообщение", {kNeut, false, kNom}));
  CHECK(has("вопрос", {kMasc, false, kNom}));
  CHECK(has("ламп", {kMasc, true, kGen}));
  CHECK(has("серий", {kMasc, true, kGen}));
  CHECK(!has("книги", {kMasc, false, kGen}));
  // Not nouns.
  CHECK(GuessNounReadings("из").empty());
  CHECK(GuessNounReadings("он").empty());
  CHECK(TestLexicon().IsNotNoun("или"));
  CHECK(TestLexicon().IsNotNoun("через"));
  CHECK(TestLexicon().IsNotNoun("только"));
  CHECK(TestLexicon().IsNotNoun("плюс"));
  CHECK(!TestLexicon().IsNotNoun("лампа"));
  CHECK(GuessNounReadings("подъедет").empty());
  CHECK(GuessNounReadings("включится").empty());
  CHECK(!GuessNounReadings("комнат").empty());
  CHECK(GuessNounReadings("новых").empty());
  CHECK(GuessNounReadings("wifi").empty());
  CHECK(GuessNounReadings("утро").size() == 2);
  CHECK(LooksLikeAdjective("новое"));
  CHECK(LooksLikeAdjective("первых"));
  CHECK(!LooksLikeAdjective("их"));
  CHECK(!LooksLikeAdjective("лампа"));
}

TEST_CASE("vits agreement: number before a noun", "[tts][vits][agreement]") {
  // The noun decides the case and the gender.
  CHECK(Read(1, "", "минута") == "одн+а");
  CHECK(Read(1, "", "минуту") == "одн+у");
  CHECK(Read(1, "", "окно") == "одн+о");
  CHECK(Read(1, "", "день") == "од+ин");
  CHECK(Read(1, "", "дверь") == "одн+а");
  CHECK(Read(2, "", "минуты") == "две");
  CHECK(Read(2, "", "часа") == "два");
  CHECK(Read(2, "", "окна") == "два");
  CHECK(Read(5, "", "минут") == "пять");
  CHECK(Read(3, "", "часам") == "трём");
  CHECK(Read(2, "", "часами") == "двум+я");
  CHECK(Read(2, "", "часах") == "двух");
  // The governing word decides among the noun's cases.
  CHECK(Read(5, "до", "минут") == "пят+и");
  CHECK(Read(5, "", "минут") == "пять");
  CHECK(Read(2, "с", "друзьями") == "двум+я");
  CHECK(Read(2, "с", "") == "двух");
  CHECK(Read(2, "о", "") == "двух");
  CHECK(Read(2, "за", "") == "два");
  CHECK(Read(1, "к", "минуте") == "одн+ой");
  CHECK(Read(1, "о", "доме") == "одн+ом");
  CHECK(Read(1, "", "розу") == "одн+у");
  // A noun that does not fit is left out; the governing word that does not fit too.
  CHECK(Read(5, "в", "утра") == "пять");
  CHECK(Read(5, "дом", "") == "пять");
  // Nouns that take an ordinal.
  CHECK(Read(5, "на", "этаже") == "п+ятом");
  CHECK(Read(5, "", "этажей") == "пять");
  CHECK(Read(3, "", "этажа") == "три");
  CHECK(Read(2, "с", "этажа") == "втор+ого");
  CHECK(Read(1, "на", "этаже") == "п+ервом");
  CHECK(Read(21, "", "век") == "дв+адцать п+ервый");
  CHECK(Read(21, "", "год") == "дв+адцать од+ин");
  CHECK(Read(2021, "в", "году") == "две т+ысячи дв+адцать п+ервом");
  CHECK(Read(2025, "", "год") == "две т+ысячи дв+адцать п+ятый");
  CHECK(Read(30, "", "лет") == "тр+идцать");
  CHECK(Read(10, "до", "числа") == "дес+ятого");
  // Suffixes.
  CHECK(Read(2, "на", "этаже", "м") == "втор+ом");
  CHECK(Read(5, "", "серия", "я") == "п+ятая");
  CHECK(Read(3, "", "место", "е") == "тр+етье");
  CHECK(Read(25, "", "кадр", "й") == "дв+адцать п+ятый");
  CHECK(Read(1, "", "января", "го") == "п+ервого");
  CHECK(Read(1990, "в", "годах", "х") == "т+ысяча девятьс+от девян+остых");
  CHECK(Read(5, "", "", "ти") == "пят+и");
  CHECK(Read(2, "", "", "х") == "двух");
  CHECK(Read(5, "", "", "ыы") == "пять");
}

TEST_CASE("vits agreement: counted nouns", "[tts][vits][agreement]") {
  const auto& minute = *TestLexicon().FindNoun("минута")->noun;
  const auto& hour = *TestLexicon().FindNoun("час")->noun;
  const NumeralReading nominative;
  NumeralReading dative;
  dative.grammatical_case = kDat;
  CHECK(CountedNoun(minute, 1, nominative) == "мин+ута");
  CHECK(CountedNoun(minute, 2, nominative) == "мин+уты");
  CHECK(CountedNoun(minute, 5, nominative) == "мин+ут");
  CHECK(CountedNoun(minute, 11, nominative) == "мин+ут");
  CHECK(CountedNoun(minute, 21000, nominative) == "мин+ут");
  CHECK(CountedNoun(minute, 1, dative) == "мин+уте");
  CHECK(CountedNoun(minute, 2, dative) == "мин+утам");
  CHECK(CountedNoun(minute, 5, dative) == "мин+утам");
  CHECK(CountedNoun(hour, 2, nominative) == "час+а");
  CHECK(NounAfterDecimal(hour) == "ч+аса");
  NumeralReading ordinal;
  ordinal.ordinal = true;
  ordinal.grammatical_case = kPre;
  CHECK(CountedNoun(hour, 5, ordinal) == "ч+асе");
}

TEST_CASE("vits lexicon: lookups", "[tts][vits][agreement]") {
  const auto& lexicon = TestLexicon();
  REQUIRE(lexicon.FindNoun("рублём") != nullptr);
  CHECK(lexicon.FindNoun("рублем")->noun == lexicon.FindNoun("рублём")->noun);
  CHECK(lexicon.FindNoun("рубл+ем")->noun == lexicon.FindNoun("рублём")->noun);
  CHECK(lexicon.FindNoun("рубль")->noun->subunit == lexicon.FindNoun("копейка")->noun);
  CHECK(lexicon.FindNoun("году")->noun->ordinal_use == OrdinalUse::OBLIQUE);
  CHECK(lexicon.FindNoun("этажа")->noun->ordinal_use == OrdinalUse::ALWAYS);
  // Alternatives are recognized, the first one is spoken.
  CHECK(lexicon.FindNoun("годов")->noun == lexicon.FindNoun("лет")->noun);
  CHECK(lexicon.FindNoun("год")->noun->plural[static_cast<int>(kGen)] == "год+ов");
  CHECK(lexicon.FindNoun("нет такого") == nullptr);
  REQUIRE(lexicon.FindUnit("В") != nullptr);
  CHECK(lexicon.FindUnit("в") == nullptr);
  CHECK(lexicon.FindUnit("км/ч")->tail == "в час");
  CHECK(lexicon.FindUnit("км")->tail.empty());
  CHECK(LookupKey("Ёж+ик") == "Ёжик");
  CHECK(LookupKey("ёж+ик") == "ежик");
}
