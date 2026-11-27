#include "giga_edge/modules/tts/interpreter/backends/vits/text/numerals.h"

#include <catch.hpp>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

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

// The six case forms, nominative to prepositional, separated by '|'.
std::vector<std::string> SplitForms(const std::string& forms) {
  std::vector<std::string> result(1);
  for (const char symbol : forms) {
    if (symbol == '|') {
      result.emplace_back();
    } else {
      result.back() += symbol;
    }
  }
  return result;
}

struct CardinalParadigm {
  uint64_t value;
  Gender gender;
  const char* forms;
};

// A number for every row of the cardinal tables and every form of the group nouns.
const CardinalParadigm kCardinalParadigms[] = {
  {0ULL, kMasc, "ноль|нул+я|нул+ю|ноль|нулём|нул+е"},
  {1ULL, kMasc, "од+ин|одног+о|одном+у|од+ин|одн+им|одн+ом"},
  {1ULL, kFem, "одн+а|одн+ой|одн+ой|одн+у|одн+ой|одн+ой"},
  {1ULL, kNeut, "одн+о|одног+о|одном+у|одн+о|одн+им|одн+ом"},
  {2ULL, kMasc, "два|двух|двум|два|двум+я|двух"},
  {2ULL, kFem, "две|двух|двум|две|двум+я|двух"},
  {3ULL, kMasc, "три|трёх|трём|три|трем+я|трёх"},
  {4ULL, kMasc, "чет+ыре|четырёх|четырём|чет+ыре|четырьм+я|четырёх"},
  {5ULL, kMasc, "пять|пят+и|пят+и|пять|пять+ю|пят+и"},
  {6ULL, kMasc, "шесть|шест+и|шест+и|шесть|шесть+ю|шест+и"},
  {7ULL, kMasc, "семь|сем+и|сем+и|семь|семь+ю|сем+и"},
  {8ULL, kMasc, "в+осемь|восьм+и|восьм+и|в+осемь|восемь+ю|восьм+и"},
  {9ULL, kMasc, "д+евять|девят+и|девят+и|д+евять|девять+ю|девят+и"},
  {10ULL, kMasc, "д+есять|десят+и|десят+и|д+есять|десять+ю|десят+и"},
  {11ULL, kMasc, "од+иннадцать|од+иннадцати|од+иннадцати|од+иннадцать|од+иннадцатью|од+иннадцати"},
  {12ULL, kMasc, "двен+адцать|двен+адцати|двен+адцати|двен+адцать|двен+адцатью|двен+адцати"},
  {13ULL, kMasc, "трин+адцать|трин+адцати|трин+адцати|трин+адцать|трин+адцатью|трин+адцати"},
  {14ULL, kMasc, "чет+ырнадцать|чет+ырнадцати|чет+ырнадцати|чет+ырнадцать|чет+ырнадцатью|чет+ырнадцати"},
  {15ULL, kMasc, "пятн+адцать|пятн+адцати|пятн+адцати|пятн+адцать|пятн+адцатью|пятн+адцати"},
  {16ULL, kMasc, "шестн+адцать|шестн+адцати|шестн+адцати|шестн+адцать|шестн+адцатью|шестн+адцати"},
  {17ULL, kMasc, "семн+адцать|семн+адцати|семн+адцати|семн+адцать|семн+адцатью|семн+адцати"},
  {18ULL, kMasc, "восемн+адцать|восемн+адцати|восемн+адцати|восемн+адцать|восемн+адцатью|восемн+адцати"},
  {19ULL, kMasc, "девятн+адцать|девятн+адцати|девятн+адцати|девятн+адцать|девятн+адцатью|девятн+адцати"},
  {20ULL, kMasc, "дв+адцать|двадцат+и|двадцат+и|дв+адцать|двадцать+ю|двадцат+и"},
  {30ULL, kMasc, "тр+идцать|тридцат+и|тридцат+и|тр+идцать|тридцать+ю|тридцат+и"},
  {40ULL, kMasc, "с+орок|сорок+а|сорок+а|с+орок|сорок+а|сорок+а"},
  {50ULL, kMasc, "пятьдес+ят|пят+идесяти|пят+идесяти|пятьдес+ят|пять+юдесятью|пят+идесяти"},
  {60ULL, kMasc, "шестьдес+ят|шест+идесяти|шест+идесяти|шестьдес+ят|шесть+юдесятью|шест+идесяти"},
  {70ULL, kMasc, "с+емьдесят|сем+идесяти|сем+идесяти|с+емьдесят|семь+юдесятью|сем+идесяти"},
  {80ULL, kMasc, "в+осемьдесят|восьм+идесяти|восьм+идесяти|в+осемьдесят|восемь+юдесятью|восьм+идесяти"},
  {90ULL, kMasc, "девян+осто|девян+оста|девян+оста|девян+осто|девян+оста|девян+оста"},
  {100ULL, kMasc, "сто|ста|ста|сто|ста|ста"},
  {200ULL, kMasc, "дв+ести|двухс+от|двумст+ам|дв+ести|двумяст+ами|двухст+ах"},
  {300ULL, kMasc, "тр+иста|трёхс+от|трёмст+ам|тр+иста|тремяст+ами|трёхст+ах"},
  {400ULL, kMasc, "чет+ыреста|четырёхс+от|четырёмст+ам|чет+ыреста|четырьмяст+ами|четырёхст+ах"},
  {500ULL, kMasc, "пятьс+от|пятис+от|пятист+ам|пятьс+от|пятьюст+ами|пятист+ах"},
  {600ULL, kMasc, "шестьс+от|шестис+от|шестист+ам|шестьс+от|шестьюст+ами|шестист+ах"},
  {700ULL, kMasc, "семьс+от|семис+от|семист+ам|семьс+от|семьюст+ами|семист+ах"},
  {800ULL, kMasc, "восемьс+от|восьмис+от|восьмист+ам|восемьс+от|восемьюст+ами|восьмист+ах"},
  {900ULL, kMasc, "девятьс+от|девятис+от|девятист+ам|девятьс+от|девятьюст+ами|девятист+ах"},
  {1000ULL, kMasc, "т+ысяча|т+ысячи|т+ысяче|т+ысячу|т+ысячей|т+ысяче"},
  {2000ULL, kMasc, "две т+ысячи|двух т+ысяч|двум т+ысячам|две т+ысячи|двум+я т+ысячами|двух т+ысячах"},
  {5000ULL, kMasc, "пять т+ысяч|пят+и т+ысяч|пят+и т+ысячам|пять т+ысяч|пять+ю т+ысячами|пят+и т+ысячах"},
  {1000000ULL, kMasc, "милли+он|милли+она|милли+ону|милли+он|милли+оном|милли+оне"},
  {2000000ULL, kMasc, "два милли+она|двух милли+онов|двум милли+онам|два милли+она|двум+я милли+онами|двух милли+онах"},
  {5000000ULL,
   kMasc,
   "пять милли+онов|пят+и милли+онов|пят+и милли+онам|пять милли+онов|пять+ю милли+онами|пят+и милли+онах"},
  {1000000000ULL, kMasc, "милли+ард|милли+арда|милли+арду|милли+ард|милли+ардом|милли+арде"},
  {2000000000ULL,
   kMasc,
   "два милли+арда|двух милли+ардов|двум милли+ардам|два милли+арда|двум+я милли+ардами|двух милли+ардах"},
  {5000000000ULL,
   kMasc,
   "пять милли+ардов|пят+и милли+ардов|пят+и милли+ардам|пять милли+ардов|пять+ю милли+ардами|пят+и милли+ардах"},
  {1000000000000ULL, kMasc, "трилли+он|трилли+она|трилли+ону|трилли+он|трилли+оном|трилли+оне"},
  {2000000000000ULL,
   kMasc,
   "два трилли+она|двух трилли+онов|двум трилли+онам|два трилли+она|двум+я трилли+онами|двух трилли+онах"},
  {5000000000000ULL,
   kMasc,
   "пять трилли+онов|пят+и трилли+онов|пят+и трилли+онам|пять трилли+онов|пять+ю трилли+онами|пят+и трилли+онах"},
};

struct OrdinalParadigm {
  uint64_t value;
  Gender gender;
  bool plural;
  const char* forms;
};

// Every ordinal stem in the masculine, and every set of endings of the three declension types.
const OrdinalParadigm kOrdinalParadigms[] = {
  {0ULL, kMasc, false, "нулев+ой|нулев+ого|нулев+ому|нулев+ой|нулев+ым|нулев+ом"},
  {1ULL, kMasc, false, "п+ервый|п+ервого|п+ервому|п+ервый|п+ервым|п+ервом"},
  {2ULL, kMasc, false, "втор+ой|втор+ого|втор+ому|втор+ой|втор+ым|втор+ом"},
  {3ULL, kMasc, false, "тр+етий|тр+етьего|тр+етьему|тр+етий|тр+етьим|тр+етьем"},
  {4ULL, kMasc, false, "четвёртый|четвёртого|четвёртому|четвёртый|четвёртым|четвёртом"},
  {5ULL, kMasc, false, "п+ятый|п+ятого|п+ятому|п+ятый|п+ятым|п+ятом"},
  {6ULL, kMasc, false, "шест+ой|шест+ого|шест+ому|шест+ой|шест+ым|шест+ом"},
  {7ULL, kMasc, false, "седьм+ой|седьм+ого|седьм+ому|седьм+ой|седьм+ым|седьм+ом"},
  {8ULL, kMasc, false, "восьм+ой|восьм+ого|восьм+ому|восьм+ой|восьм+ым|восьм+ом"},
  {9ULL, kMasc, false, "дев+ятый|дев+ятого|дев+ятому|дев+ятый|дев+ятым|дев+ятом"},
  {10ULL, kMasc, false, "дес+ятый|дес+ятого|дес+ятому|дес+ятый|дес+ятым|дес+ятом"},
  {11ULL, kMasc, false, "од+иннадцатый|од+иннадцатого|од+иннадцатому|од+иннадцатый|од+иннадцатым|од+иннадцатом"},
  {12ULL, kMasc, false, "двен+адцатый|двен+адцатого|двен+адцатому|двен+адцатый|двен+адцатым|двен+адцатом"},
  {13ULL, kMasc, false, "трин+адцатый|трин+адцатого|трин+адцатому|трин+адцатый|трин+адцатым|трин+адцатом"},
  {14ULL, kMasc, false, "чет+ырнадцатый|чет+ырнадцатого|чет+ырнадцатому|чет+ырнадцатый|чет+ырнадцатым|чет+ырнадцатом"},
  {15ULL, kMasc, false, "пятн+адцатый|пятн+адцатого|пятн+адцатому|пятн+адцатый|пятн+адцатым|пятн+адцатом"},
  {16ULL, kMasc, false, "шестн+адцатый|шестн+адцатого|шестн+адцатому|шестн+адцатый|шестн+адцатым|шестн+адцатом"},
  {17ULL, kMasc, false, "семн+адцатый|семн+адцатого|семн+адцатому|семн+адцатый|семн+адцатым|семн+адцатом"},
  {18ULL, kMasc, false, "восемн+адцатый|восемн+адцатого|восемн+адцатому|восемн+адцатый|восемн+адцатым|восемн+адцатом"},
  {19ULL, kMasc, false, "девятн+адцатый|девятн+адцатого|девятн+адцатому|девятн+адцатый|девятн+адцатым|девятн+адцатом"},
  {20ULL, kMasc, false, "двадц+атый|двадц+атого|двадц+атому|двадц+атый|двадц+атым|двадц+атом"},
  {30ULL, kMasc, false, "тридц+атый|тридц+атого|тридц+атому|тридц+атый|тридц+атым|тридц+атом"},
  {40ULL, kMasc, false, "сороков+ой|сороков+ого|сороков+ому|сороков+ой|сороков+ым|сороков+ом"},
  {50ULL, kMasc, false, "пятидес+ятый|пятидес+ятого|пятидес+ятому|пятидес+ятый|пятидес+ятым|пятидес+ятом"},
  {60ULL, kMasc, false, "шестидес+ятый|шестидес+ятого|шестидес+ятому|шестидес+ятый|шестидес+ятым|шестидес+ятом"},
  {70ULL, kMasc, false, "семидес+ятый|семидес+ятого|семидес+ятому|семидес+ятый|семидес+ятым|семидес+ятом"},
  {80ULL, kMasc, false, "восьмидес+ятый|восьмидес+ятого|восьмидес+ятому|восьмидес+ятый|восьмидес+ятым|восьмидес+ятом"},
  {90ULL, kMasc, false, "девян+остый|девян+остого|девян+остому|девян+остый|девян+остым|девян+остом"},
  {100ULL, kMasc, false, "с+отый|с+отого|с+отому|с+отый|с+отым|с+отом"},
  {200ULL, kMasc, false, "двухс+отый|двухс+отого|двухс+отому|двухс+отый|двухс+отым|двухс+отом"},
  {300ULL, kMasc, false, "трёхс+отый|трёхс+отого|трёхс+отому|трёхс+отый|трёхс+отым|трёхс+отом"},
  {400ULL, kMasc, false, "четырёхс+отый|четырёхс+отого|четырёхс+отому|четырёхс+отый|четырёхс+отым|четырёхс+отом"},
  {500ULL, kMasc, false, "пятис+отый|пятис+отого|пятис+отому|пятис+отый|пятис+отым|пятис+отом"},
  {600ULL, kMasc, false, "шестис+отый|шестис+отого|шестис+отому|шестис+отый|шестис+отым|шестис+отом"},
  {700ULL, kMasc, false, "семис+отый|семис+отого|семис+отому|семис+отый|семис+отым|семис+отом"},
  {800ULL, kMasc, false, "восьмис+отый|восьмис+отого|восьмис+отому|восьмис+отый|восьмис+отым|восьмис+отом"},
  {900ULL, kMasc, false, "девятис+отый|девятис+отого|девятис+отому|девятис+отый|девятис+отым|девятис+отом"},
  {1000ULL, kMasc, false, "т+ысячный|т+ысячного|т+ысячному|т+ысячный|т+ысячным|т+ысячном"},
  {1000000ULL, kMasc, false, "милли+онный|милли+онного|милли+онному|милли+онный|милли+онным|милли+онном"},
  {1000000000ULL, kMasc, false, "милли+ардный|милли+ардного|милли+ардному|милли+ардный|милли+ардным|милли+ардном"},
  {1000000000000ULL, kMasc, false, "трилли+онный|трилли+онного|трилли+онному|трилли+онный|трилли+онным|трилли+онном"},
  {1ULL, kFem, false, "п+ервая|п+ервой|п+ервой|п+ервую|п+ервой|п+ервой"},
  {1ULL, kNeut, false, "п+ервое|п+ервого|п+ервому|п+ервое|п+ервым|п+ервом"},
  {1ULL, kMasc, true, "п+ервые|п+ервых|п+ервым|п+ервые|п+ервыми|п+ервых"},
  {2ULL, kFem, false, "втор+ая|втор+ой|втор+ой|втор+ую|втор+ой|втор+ой"},
  {2ULL, kNeut, false, "втор+ое|втор+ого|втор+ому|втор+ое|втор+ым|втор+ом"},
  {2ULL, kMasc, true, "втор+ые|втор+ых|втор+ым|втор+ые|втор+ыми|втор+ых"},
  {3ULL, kFem, false, "тр+етья|тр+етьей|тр+етьей|тр+етью|тр+етьей|тр+етьей"},
  {3ULL, kNeut, false, "тр+етье|тр+етьего|тр+етьему|тр+етье|тр+етьим|тр+етьем"},
  {3ULL, kMasc, true, "тр+етьи|тр+етьих|тр+етьим|тр+етьи|тр+етьими|тр+етьих"},
  {1ULL, kFem, true, "п+ервые|п+ервых|п+ервым|п+ервые|п+ервыми|п+ервых"},
  {20ULL, kMasc, true, "двадц+атые|двадц+атых|двадц+атым|двадц+атые|двадц+атыми|двадц+атых"},
  {100ULL, kMasc, true, "с+отые|с+отых|с+отым|с+отые|с+отыми|с+отых"},
  {2000ULL, kMasc, true, "двухт+ысячные|двухт+ысячных|двухт+ысячным|двухт+ысячные|двухт+ысячными|двухт+ысячных"},
};

bool IsVowel(const std::string& letter) {
  static const std::string vowels_list = "аеёиоуыэюя";
  return vowels_list.find(letter) != std::string::npos;
}

// A word is marked correctly when it has one '+' right before a vowel other than 'ё' and more vowels than one, or no
// '+' and a 'ё' or a single vowel.
bool IsStressMarked(const std::string& word) {
  int marks = 0;
  int vowels = 0;
  bool has_yo = false;
  bool mark_before_vowel = true;
  bool after_mark = false;
  for (size_t i = 0; i < word.size();) {
    if (word[i] == '+') {
      ++marks;
      after_mark = true;
      ++i;
      continue;
    }
    const auto letter = word.substr(i, 2);
    i += 2;
    const auto vowel = IsVowel(letter);
    vowels += vowel ? 1 : 0;
    has_yo = has_yo || letter == "ё";
    if (after_mark && !vowel) {
      mark_before_vowel = false;
    }
    after_mark = false;
  }
  if (after_mark || !mark_before_vowel || marks > 1) {
    return false;
  }
  if (marks == 1) {
    return vowels > 1 && word.find("+ё") == std::string::npos;
  }
  return has_yo || vowels == 1;
}

bool AreStressMarked(const std::string& text) {
  size_t begin = 0;
  while (begin <= text.size()) {
    const auto end = std::min(text.find(' ', begin), text.size());
    if (!IsStressMarked(text.substr(begin, end - begin))) {
      return false;
    }
    begin = end + 1;
  }
  return true;
}

} // namespace

TEST_CASE("vits numerals: the form of a counted noun", "[tts][vits][numerals]") {
  CHECK(CountFormOf(0) == CountForm::MANY);
  CHECK(CountFormOf(1) == CountForm::ONE);
  CHECK(CountFormOf(2) == CountForm::FEW);
  CHECK(CountFormOf(4) == CountForm::FEW);
  CHECK(CountFormOf(5) == CountForm::MANY);
  CHECK(CountFormOf(11) == CountForm::MANY);
  CHECK(CountFormOf(12) == CountForm::MANY);
  CHECK(CountFormOf(14) == CountForm::MANY);
  CHECK(CountFormOf(21) == CountForm::ONE);
  CHECK(CountFormOf(22) == CountForm::FEW);
  CHECK(CountFormOf(25) == CountForm::MANY);
  CHECK(CountFormOf(111) == CountForm::MANY);
  CHECK(CountFormOf(112) == CountForm::MANY);
  CHECK(CountFormOf(121) == CountForm::ONE);
  CHECK(CountFormOf(1000) == CountForm::MANY);
  CHECK(CountFormOf(1001) == CountForm::ONE);
}

TEST_CASE("vits numerals: cardinals below a thousand in the nominative", "[tts][vits][numerals]") {
  CHECK(Cardinal(0, kNom, kMasc) == "ноль");
  CHECK(Cardinal(1, kNom, kMasc) == "од+ин");
  CHECK(Cardinal(1, kNom, kFem) == "одн+а");
  CHECK(Cardinal(1, kNom, kNeut) == "одн+о");
  CHECK(Cardinal(2, kNom, kMasc) == "два");
  CHECK(Cardinal(2, kNom, kFem) == "две");
  CHECK(Cardinal(2, kNom, kNeut) == "два");
  CHECK(Cardinal(3, kNom, kFem) == "три");
  CHECK(Cardinal(4, kNom, kMasc) == "чет+ыре");
  CHECK(Cardinal(8, kNom, kMasc) == "в+осемь");
  CHECK(Cardinal(10, kNom, kMasc) == "д+есять");
  CHECK(Cardinal(11, kNom, kFem) == "од+иннадцать");
  CHECK(Cardinal(12, kNom, kFem) == "двен+адцать");
  CHECK(Cardinal(14, kNom, kMasc) == "чет+ырнадцать");
  CHECK(Cardinal(19, kNom, kMasc) == "девятн+адцать");
  CHECK(Cardinal(20, kNom, kMasc) == "дв+адцать");
  CHECK(Cardinal(21, kNom, kFem) == "дв+адцать одн+а");
  CHECK(Cardinal(22, kNom, kFem) == "дв+адцать две");
  CHECK(Cardinal(22, kNom, kMasc) == "дв+адцать два");
  CHECK(Cardinal(40, kNom, kMasc) == "с+орок");
  CHECK(Cardinal(50, kNom, kMasc) == "пятьдес+ят");
  CHECK(Cardinal(70, kNom, kMasc) == "с+емьдесят");
  CHECK(Cardinal(90, kNom, kMasc) == "девян+осто");
  CHECK(Cardinal(99, kNom, kMasc) == "девян+осто д+евять");
  CHECK(Cardinal(100, kNom, kMasc) == "сто");
  CHECK(Cardinal(101, kNom, kNeut) == "сто одн+о");
  CHECK(Cardinal(110, kNom, kMasc) == "сто д+есять");
  CHECK(Cardinal(200, kNom, kMasc) == "дв+ести");
  CHECK(Cardinal(315, kNom, kMasc) == "тр+иста пятн+адцать");
  CHECK(Cardinal(999, kNom, kMasc) == "девятьс+от девян+осто д+евять");
}

TEST_CASE("vits numerals: cardinals in the oblique cases", "[tts][vits][numerals]") {
  CHECK(Cardinal(0, kGen, kMasc) == "нул+я");
  CHECK(Cardinal(0, kIns, kMasc) == "нулём");
  CHECK(Cardinal(1, kGen, kMasc) == "одног+о");
  CHECK(Cardinal(1, kGen, kFem) == "одн+ой");
  CHECK(Cardinal(1, kDat, kNeut) == "одном+у");
  CHECK(Cardinal(1, kAcc, kMasc) == "од+ин");
  CHECK(Cardinal(1, kAcc, kFem) == "одн+у");
  CHECK(Cardinal(1, kIns, kMasc) == "одн+им");
  CHECK(Cardinal(1, kPre, kMasc) == "одн+ом");
  CHECK(Cardinal(2, kGen, kFem) == "двух");
  CHECK(Cardinal(2, kAcc, kFem) == "две");
  CHECK(Cardinal(2, kIns, kMasc) == "двум+я");
  CHECK(Cardinal(3, kDat, kMasc) == "трём");
  CHECK(Cardinal(3, kIns, kMasc) == "трем+я");
  CHECK(Cardinal(4, kIns, kMasc) == "четырьм+я");
  CHECK(Cardinal(5, kGen, kMasc) == "пят+и");
  CHECK(Cardinal(5, kAcc, kMasc) == "пять");
  CHECK(Cardinal(5, kIns, kMasc) == "пять+ю");
  CHECK(Cardinal(7, kDat, kMasc) == "сем+и");
  CHECK(Cardinal(8, kIns, kMasc) == "восемь+ю");
  CHECK(Cardinal(12, kPre, kMasc) == "двен+адцати");
  CHECK(Cardinal(15, kIns, kMasc) == "пятн+адцатью");
  CHECK(Cardinal(22, kGen, kMasc) == "двадцат+и двух");
  CHECK(Cardinal(21, kAcc, kFem) == "дв+адцать одн+у");
  CHECK(Cardinal(40, kGen, kMasc) == "сорок+а");
  CHECK(Cardinal(40, kAcc, kMasc) == "с+орок");
  CHECK(Cardinal(50, kGen, kMasc) == "пят+идесяти");
  CHECK(Cardinal(50, kIns, kMasc) == "пять+юдесятью");
  CHECK(Cardinal(80, kDat, kMasc) == "восьм+идесяти");
  CHECK(Cardinal(90, kGen, kMasc) == "девян+оста");
  CHECK(Cardinal(100, kGen, kMasc) == "ста");
  CHECK(Cardinal(100, kAcc, kMasc) == "сто");
  CHECK(Cardinal(200, kGen, kMasc) == "двухс+от");
  CHECK(Cardinal(300, kDat, kMasc) == "трёмст+ам");
  CHECK(Cardinal(400, kIns, kMasc) == "четырьмяст+ами");
  CHECK(Cardinal(500, kPre, kMasc) == "пятист+ах");
  CHECK(Cardinal(555, kGen, kMasc) == "пятис+от пят+идесяти пят+и");
  CHECK(Cardinal(999, kIns, kMasc) == "девятьюст+ами девян+оста девять+ю");
}

TEST_CASE("vits numerals: thousands and above", "[tts][vits][numerals]") {
  CHECK(Cardinal(1000, kNom, kMasc) == "т+ысяча");
  CHECK(Cardinal(1000, kGen, kMasc) == "т+ысячи");
  CHECK(Cardinal(1000, kAcc, kMasc) == "т+ысячу");
  CHECK(Cardinal(1000, kIns, kMasc) == "т+ысячей");
  CHECK(Cardinal(1001, kNom, kFem) == "т+ысяча одн+а");
  CHECK(Cardinal(2000, kNom, kMasc) == "две т+ысячи");
  CHECK(Cardinal(2000, kGen, kMasc) == "двух т+ысяч");
  CHECK(Cardinal(2000, kDat, kMasc) == "двум т+ысячам");
  CHECK(Cardinal(2000, kAcc, kMasc) == "две т+ысячи");
  CHECK(Cardinal(2000, kIns, kMasc) == "двум+я т+ысячами");
  CHECK(Cardinal(2000, kPre, kMasc) == "двух т+ысячах");
  CHECK(Cardinal(5000, kNom, kMasc) == "пять т+ысяч");
  CHECK(Cardinal(5000, kGen, kMasc) == "пят+и т+ысяч");
  CHECK(Cardinal(5000, kDat, kMasc) == "пят+и т+ысячам");
  CHECK(Cardinal(11000, kNom, kMasc) == "од+иннадцать т+ысяч");
  CHECK(Cardinal(21000, kNom, kMasc) == "дв+адцать одн+а т+ысяча");
  CHECK(Cardinal(21000, kAcc, kMasc) == "дв+адцать одн+у т+ысячу");
  CHECK(Cardinal(21000, kGen, kMasc) == "двадцат+и одн+ой т+ысячи");
  CHECK(Cardinal(22000, kNom, kMasc) == "дв+адцать две т+ысячи");
  CHECK(Cardinal(2024, kNom, kMasc) == "две т+ысячи дв+адцать чет+ыре");
  CHECK(Cardinal(1500, kGen, kMasc) == "т+ысячи пятис+от");
  CHECK(Cardinal(100000, kNom, kMasc) == "сто т+ысяч");
  CHECK(Cardinal(1000000, kNom, kMasc) == "милли+он");
  CHECK(Cardinal(1000000, kGen, kMasc) == "милли+она");
  CHECK(Cardinal(2000000, kNom, kMasc) == "два милли+она");
  CHECK(Cardinal(2000000, kIns, kMasc) == "двум+я милли+онами");
  CHECK(Cardinal(5000000, kNom, kMasc) == "пять милли+онов");
  CHECK(Cardinal(21000000, kNom, kMasc) == "дв+адцать од+ин милли+он");
  CHECK(Cardinal(1000000000, kNom, kMasc) == "милли+ард");
  CHECK(Cardinal(3000000000ULL, kNom, kMasc) == "три милли+арда");
  CHECK(Cardinal(7000000000000ULL, kNom, kMasc) == "семь трилли+онов");
  CHECK(Cardinal(1234567, kNom, kFem) == "милли+он дв+ести тр+идцать чет+ыре т+ысячи пятьс+от шестьдес+ят семь");
  CHECK(Cardinal(1000002, kNom, kFem) == "милли+он две");
  CHECK(
    Cardinal(kMaxNumeral, kNom, kMasc)
    == "девятьс+от девян+осто д+евять трилли+онов девятьс+от девян+осто д+евять милли+ардов девятьс+от "
       "девян+осто д+евять милли+онов девятьс+от девян+осто д+евять т+ысяч девятьс+от девян+осто д+евять"
  );
  CHECK_THROWS_AS(Cardinal(kMaxNumeral + 1, kNom, kMasc), std::out_of_range);
}

TEST_CASE("vits numerals: ordinals", "[tts][vits][numerals]") {
  CHECK(Ordinal(0, kNom, kMasc) == "нулев+ой");
  CHECK(Ordinal(0, kGen, kMasc) == "нулев+ого");
  CHECK(Ordinal(1, kNom, kMasc) == "п+ервый");
  CHECK(Ordinal(1, kNom, kFem) == "п+ервая");
  CHECK(Ordinal(1, kNom, kNeut) == "п+ервое");
  CHECK(Ordinal(1, kNom, kMasc, true) == "п+ервые");
  CHECK(Ordinal(1, kGen, kMasc) == "п+ервого");
  CHECK(Ordinal(1, kAcc, kFem) == "п+ервую");
  CHECK(Ordinal(1, kPre, kMasc) == "п+ервом");
  CHECK(Ordinal(2, kNom, kMasc) == "втор+ой");
  CHECK(Ordinal(2, kGen, kMasc) == "втор+ого");
  CHECK(Ordinal(2, kNom, kFem) == "втор+ая");
  CHECK(Ordinal(2, kAcc, kFem) == "втор+ую");
  CHECK(Ordinal(2, kIns, kMasc, true) == "втор+ыми");
  CHECK(Ordinal(3, kNom, kMasc) == "тр+етий");
  CHECK(Ordinal(3, kGen, kMasc) == "тр+етьего");
  CHECK(Ordinal(3, kNom, kFem) == "тр+етья");
  CHECK(Ordinal(3, kAcc, kFem) == "тр+етью");
  CHECK(Ordinal(3, kNom, kNeut) == "тр+етье");
  CHECK(Ordinal(3, kPre, kMasc) == "тр+етьем");
  CHECK(Ordinal(3, kGen, kMasc, true) == "тр+етьих");
  CHECK(Ordinal(4, kNom, kMasc) == "четвёртый");
  CHECK(Ordinal(5, kGen, kNeut) == "п+ятого");
  CHECK(Ordinal(6, kNom, kMasc) == "шест+ой");
  CHECK(Ordinal(7, kNom, kNeut) == "седьм+ое");
  CHECK(Ordinal(8, kDat, kMasc) == "восьм+ому");
  CHECK(Ordinal(10, kNom, kMasc) == "дес+ятый");
  CHECK(Ordinal(11, kNom, kMasc) == "од+иннадцатый");
  CHECK(Ordinal(19, kNom, kFem) == "девятн+адцатая");
  CHECK(Ordinal(20, kNom, kMasc) == "двадц+атый");
  CHECK(Ordinal(21, kNom, kMasc) == "дв+адцать п+ервый");
  CHECK(Ordinal(22, kGen, kMasc) == "дв+адцать втор+ого");
  CHECK(Ordinal(30, kNom, kNeut) == "тридц+атое");
  CHECK(Ordinal(40, kNom, kMasc) == "сороков+ой");
  CHECK(Ordinal(50, kNom, kMasc) == "пятидес+ятый");
  CHECK(Ordinal(90, kNom, kMasc) == "девян+остый");
  CHECK(Ordinal(100, kNom, kMasc) == "с+отый");
  CHECK(Ordinal(101, kNom, kMasc) == "сто п+ервый");
  CHECK(Ordinal(120, kNom, kMasc) == "сто двадц+атый");
  CHECK(Ordinal(200, kNom, kMasc) == "двухс+отый");
  CHECK(Ordinal(300, kNom, kFem) == "трёхс+отая");
  CHECK(Ordinal(999, kNom, kMasc) == "девятьс+от девян+осто дев+ятый");
  CHECK(Ordinal(1000, kNom, kMasc) == "т+ысячный");
  CHECK(Ordinal(1900, kNom, kMasc) == "т+ысяча девятис+отый");
  CHECK(Ordinal(1985, kGen, kMasc) == "т+ысяча девятьс+от в+осемьдесят п+ятого");
  CHECK(Ordinal(2000, kNom, kMasc) == "двухт+ысячный");
  CHECK(Ordinal(2000, kPre, kMasc) == "двухт+ысячном");
  CHECK(Ordinal(2024, kPre, kMasc) == "две т+ысячи дв+адцать четвёртом");
  CHECK(Ordinal(2024, kGen, kMasc) == "две т+ысячи дв+адцать четвёртого");
  CHECK(Ordinal(3000, kNom, kMasc) == "трёхт+ысячный");
  CHECK(Ordinal(5000, kNom, kMasc) == "пятит+ысячный");
  CHECK(Ordinal(10000, kNom, kMasc) == "десятит+ысячный");
  CHECK(Ordinal(21000, kNom, kMasc) == "двадцатиоднот+ысячный");
  CHECK(Ordinal(40000, kNom, kMasc) == "сорокат+ысячный");
  CHECK(Ordinal(90000, kNom, kMasc) == "девяностот+ысячный");
  CHECK(Ordinal(100000, kNom, kMasc) == "стот+ысячный");
  CHECK(Ordinal(200000, kNom, kMasc) == "двухсотт+ысячный");
  CHECK(Ordinal(1000000, kNom, kMasc) == "милли+онный");
  CHECK(Ordinal(2000000, kNom, kFem) == "двухмилли+онная");
  CHECK(Ordinal(1200000, kNom, kMasc) == "милли+он двухсотт+ысячный");
  CHECK_THROWS_AS(Ordinal(kMaxNumeral + 1, kNom, kMasc), std::out_of_range);
}

TEST_CASE("vits numerals: decimal fractions", "[tts][vits][numerals]") {
  CHECK(Decimal(15, "6", kNom) == "пятн+адцать ц+елых шесть дес+ятых");
  CHECK(Decimal(0, "5", kNom) == "ноль ц+елых пять дес+ятых");
  CHECK(Decimal(1, "5", kNom) == "одн+а ц+елая пять дес+ятых");
  CHECK(Decimal(1, "5", kAcc) == "одн+у ц+елую пять дес+ятых");
  CHECK(Decimal(2, "1", kNom) == "две ц+елых одн+а дес+ятая");
  CHECK(Decimal(21, "2", kNom) == "дв+адцать одн+а ц+елая две дес+ятых");
  CHECK(Decimal(2, "25", kNom) == "две ц+елых дв+адцать пять с+отых");
  CHECK(Decimal(36, "06", kNom) == "тр+идцать шесть ц+елых шесть с+отых");
  CHECK(Decimal(3, "141", kNom) == "три ц+елых сто с+орок одн+а т+ысячная");
  CHECK(Decimal(3, "0", kNom) == "три ц+елых ноль дес+ятых");
  CHECK(Decimal(11, "11", kNom) == "од+иннадцать ц+елых од+иннадцать с+отых");
  CHECK(Decimal(1, "5", kGen) == "одн+ой ц+елой пят+и дес+ятых");
  CHECK(Decimal(2, "5", kGen) == "двух ц+елых пят+и дес+ятых");
  CHECK(Decimal(2, "5", kDat) == "двум ц+елым пят+и дес+ятым");
  CHECK(Decimal(2, "1", kIns) == "двум+я ц+елыми одн+ой дес+ятой");
  CHECK_THROWS_AS(Decimal(1, "", kNom), std::invalid_argument);
  CHECK_THROWS_AS(Decimal(1, "1234", kNom), std::invalid_argument);
  CHECK_THROWS_AS(Decimal(1, "1a", kNom), std::invalid_argument);
  CHECK_THROWS_AS(Decimal(kMaxNumeral + 1, "5", kNom), std::out_of_range);
}

TEST_CASE("vits numerals: every row of the cardinal tables in every case", "[tts][vits][numerals]") {
  for (const auto& paradigm : kCardinalParadigms) {
    const auto forms = SplitForms(paradigm.forms);
    REQUIRE(forms.size() == kCaseCount);
    for (int case_index = 0; case_index < kCaseCount; ++case_index) {
      INFO(paradigm.value << ", case " << case_index);
      CHECK(Cardinal(paradigm.value, static_cast<GrammaticalCase>(case_index), paradigm.gender) == forms[case_index]);
    }
  }
}

TEST_CASE("vits numerals: every ordinal stem and ending in every case", "[tts][vits][numerals]") {
  for (const auto& paradigm : kOrdinalParadigms) {
    const auto forms = SplitForms(paradigm.forms);
    REQUIRE(forms.size() == kCaseCount);
    for (int case_index = 0; case_index < kCaseCount; ++case_index) {
      INFO(paradigm.value << ", case " << case_index);
      CHECK(
        Ordinal(paradigm.value, static_cast<GrammaticalCase>(case_index), paradigm.gender, paradigm.plural)
        == forms[case_index]
      );
    }
  }
}

TEST_CASE("vits numerals: a one is dropped only at the start of a number", "[tts][vits][numerals]") {
  CHECK(Cardinal(1001000, kNom, kMasc) == "милли+он одн+а т+ысяча");
  CHECK(Cardinal(2001000, kNom, kMasc) == "два милли+она одн+а т+ысяча");
  CHECK(Cardinal(2001000, kGen, kMasc) == "двух милли+онов одн+ой т+ысячи");
  CHECK(Cardinal(3001000000ULL, kNom, kMasc) == "три милли+арда од+ин милли+он");
  CHECK(Cardinal(5001001, kNom, kMasc) == "пять милли+онов одн+а т+ысяча од+ин");
  CHECK(Ordinal(2001000, kNom, kMasc) == "два милли+она т+ысячный");
}

TEST_CASE("vits numerals: compound ordinals of round thousands", "[tts][vits][numerals]") {
  CHECK(Ordinal(20000, kNom, kMasc) == "двадцатит+ысячный");
  CHECK(Ordinal(101000, kNom, kMasc) == "стооднот+ысячный");
  CHECK(Ordinal(111000, kNom, kMasc) == "стоодиннадцатит+ысячный");
  CHECK(Ordinal(303000, kNom, kMasc) == "трёхсоттрёхт+ысячный");
  CHECK(Ordinal(999000, kNom, kMasc) == "девятисотдевяностодевятит+ысячный");
  CHECK(Ordinal(1000000000, kNom, kMasc) == "милли+ардный");
  CHECK(Ordinal(1990, kGen, kMasc, true) == "т+ысяча девятьс+от девян+остых");
  CHECK(
    Ordinal(kMaxNumeral, kNom, kMasc)
    == "девятьс+от девян+осто д+евять трилли+онов девятьс+от девян+осто д+евять милли+ардов девятьс+от "
       "девян+осто д+евять милли+онов девятьс+от девян+осто д+евять т+ысяч девятьс+от девян+осто дев+ятый"
  );
}

TEST_CASE(
  "vits numerals: decimal fractions with the digits 8 and 9 and a one in the oblique cases", "[tts][vits][numerals]"
) {
  CHECK(Decimal(1, "9", kNom) == "одн+а ц+елая д+евять дес+ятых");
  CHECK(Decimal(1, "5", kIns) == "одн+ой ц+елой пять+ю дес+ятыми");
  CHECK(Decimal(1, "5", kPre) == "одн+ой ц+елой пят+и дес+ятых");
  CHECK(Decimal(8, "8", kDat) == "восьм+и ц+елым восьм+и дес+ятым");
}

TEST_CASE("vits numerals: every word carries one stress mark, a 'ё' or a single vowel", "[tts][vits][numerals]") {
  REQUIRE(IsStressMarked("дв+адцать"));
  REQUIRE(IsStressMarked("трёхс+от"));
  REQUIRE(IsStressMarked("четвёртый"));
  REQUIRE(IsStressMarked("два"));
  REQUIRE_FALSE(IsStressMarked("двадцать"));
  REQUIRE_FALSE(IsStressMarked("дв+адц+ать"));
  REQUIRE_FALSE(IsStressMarked("тридцатью+"));
  REQUIRE_FALSE(IsStressMarked("д+вадцать"));
  REQUIRE_FALSE(IsStressMarked("дв+а"));
  REQUIRE_FALSE(IsStressMarked("четв+ёртый"));
  for (uint64_t value = 0; value < 2000; ++value) {
    for (int case_index = 0; case_index < kCaseCount; ++case_index) {
      const auto grammatical_case = static_cast<GrammaticalCase>(case_index);
      for (const auto gender : {kMasc, kFem, kNeut}) {
        INFO(value << ", case " << case_index << ", gender " << static_cast<int>(gender));
        REQUIRE(AreStressMarked(Cardinal(value * 1001, grammatical_case, gender)));
        REQUIRE(AreStressMarked(Ordinal(value, grammatical_case, gender)));
        REQUIRE(AreStressMarked(Ordinal(value * 1000, grammatical_case, gender, true)));
      }
      REQUIRE(AreStressMarked(Decimal(value, std::to_string(value % 1000), grammatical_case)));
    }
  }
}
