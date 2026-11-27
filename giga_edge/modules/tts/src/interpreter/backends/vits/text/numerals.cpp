#include "giga_edge/modules/tts/interpreter/backends/vits/text/numerals.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace giga_edge::vits {

namespace {

// Nominative, genitive, dative, accusative, instrumental, prepositional.
using Forms = std::array<std::string_view, kCaseCount>;

constexpr int kNominative = static_cast<int>(GrammaticalCase::NOMINATIVE);
constexpr int kGenitive = static_cast<int>(GrammaticalCase::GENITIVE);
constexpr int kAccusative = static_cast<int>(GrammaticalCase::ACCUSATIVE);

constexpr Forms kZero = {"ноль", "нул+я", "нул+ю", "ноль", "нулём", "нул+е"}; // "ниже нуля", "равно нулю"

// By gender.
constexpr std::array<Forms, 3> kOne = {{
  {"од+ин", "одног+о", "одном+у", "од+ин", "одн+им", "одн+ом"},
  {"одн+а", "одн+ой", "одн+ой", "одн+у", "одн+ой", "одн+ой"},
  {"одн+о", "одног+о", "одном+у", "одн+о", "одн+им", "одн+ом"},
}};

constexpr Forms kTwoFeminine = {"две", "двух", "двум", "две", "двум+я", "двух"};

// By value; 0 and 1 are not used.
constexpr std::array<Forms, 20> kUnits = {{
  {},
  {},
  {"два", "двух", "двум", "два", "двум+я", "двух"},
  {"три", "трёх", "трём", "три", "трем+я", "трёх"},
  {"чет+ыре", "четырёх", "четырём", "чет+ыре", "четырьм+я", "четырёх"},
  {"пять", "пят+и", "пят+и", "пять", "пять+ю", "пят+и"},
  {"шесть", "шест+и", "шест+и", "шесть", "шесть+ю", "шест+и"},
  {"семь", "сем+и", "сем+и", "семь", "семь+ю", "сем+и"},
  {"в+осемь", "восьм+и", "восьм+и", "в+осемь", "восемь+ю", "восьм+и"},
  {"д+евять", "девят+и", "девят+и", "д+евять", "девять+ю", "девят+и"},
  {"д+есять", "десят+и", "десят+и", "д+есять", "десять+ю", "десят+и"},
  {"од+иннадцать", "од+иннадцати", "од+иннадцати", "од+иннадцать", "од+иннадцатью", "од+иннадцати"},
  {"двен+адцать", "двен+адцати", "двен+адцати", "двен+адцать", "двен+адцатью", "двен+адцати"},
  {"трин+адцать", "трин+адцати", "трин+адцати", "трин+адцать", "трин+адцатью", "трин+адцати"},
  {"чет+ырнадцать", "чет+ырнадцати", "чет+ырнадцати", "чет+ырнадцать", "чет+ырнадцатью", "чет+ырнадцати"},
  {"пятн+адцать", "пятн+адцати", "пятн+адцати", "пятн+адцать", "пятн+адцатью", "пятн+адцати"},
  {"шестн+адцать", "шестн+адцати", "шестн+адцати", "шестн+адцать", "шестн+адцатью", "шестн+адцати"},
  {"семн+адцать", "семн+адцати", "семн+адцати", "семн+адцать", "семн+адцатью", "семн+адцати"},
  {"восемн+адцать", "восемн+адцати", "восемн+адцати", "восемн+адцать", "восемн+адцатью", "восемн+адцати"},
  {"девятн+адцать", "девятн+адцати", "девятн+адцати", "девятн+адцать", "девятн+адцатью", "девятн+адцати"},
}};

// By the tens digit; 0 and 1 are not used.
constexpr std::array<Forms, 10> kTens = {{
  {},
  {},
  {"дв+адцать", "двадцат+и", "двадцат+и", "дв+адцать", "двадцать+ю", "двадцат+и"},
  {"тр+идцать", "тридцат+и", "тридцат+и", "тр+идцать", "тридцать+ю", "тридцат+и"},
  {"с+орок", "сорок+а", "сорок+а", "с+орок", "сорок+а", "сорок+а"},
  {"пятьдес+ят", "пят+идесяти", "пят+идесяти", "пятьдес+ят", "пять+юдесятью", "пят+идесяти"},
  {"шестьдес+ят", "шест+идесяти", "шест+идесяти", "шестьдес+ят", "шесть+юдесятью", "шест+идесяти"},
  {"с+емьдесят", "сем+идесяти", "сем+идесяти", "с+емьдесят", "семь+юдесятью", "сем+идесяти"},
  {"в+осемьдесят", "восьм+идесяти", "восьм+идесяти", "в+осемьдесят", "восемь+юдесятью", "восьм+идесяти"},
  {"девян+осто", "девян+оста", "девян+оста", "девян+осто", "девян+оста", "девян+оста"},
}};

// By the hundreds digit; 0 is not used.
constexpr std::array<Forms, 10> kHundreds = {{
  {},
  {"сто", "ста", "ста", "сто", "ста", "ста"},
  {"дв+ести", "двухс+от", "двумст+ам", "дв+ести", "двумяст+ами", "двухст+ах"},
  {"тр+иста", "трёхс+от", "трёмст+ам", "тр+иста", "тремяст+ами", "трёхст+ах"},
  {"чет+ыреста", "четырёхс+от", "четырёмст+ам", "чет+ыреста", "четырьмяст+ами", "четырёхст+ах"},
  {"пятьс+от", "пятис+от", "пятист+ам", "пятьс+от", "пятьюст+ами", "пятист+ах"},
  {"шестьс+от", "шестис+от", "шестист+ам", "шестьс+от", "шестьюст+ами", "шестист+ах"},
  {"семьс+от", "семис+от", "семист+ам", "семьс+от", "семьюст+ами", "семист+ах"},
  {"восемьс+от", "восьмис+от", "восьмист+ам", "восемьс+от", "восемьюст+ами", "восьмист+ах"},
  {"девятьс+от", "девятис+от", "девятист+ам", "девятьс+от", "девятьюст+ами", "девятист+ах"},
}};

// The noun of a group of three digits: thousands, millions and so on.
struct GroupNoun {
  Gender gender;
  Forms singular;
  Forms plural;
  std::string_view ordinal_stem;
};

constexpr int kGroupCount = 5;
static_assert(kMaxNumeral == 999'999'999'999'999ULL, "kGroupCount groups of three digits");
// By the group; the group of units has no noun.
constexpr std::array<GroupNoun, kGroupCount> kGroupNouns = {{
  {},
  {
    Gender::FEMININE,
    {"т+ысяча", "т+ысячи", "т+ысяче", "т+ысячу", "т+ысячей", "т+ысяче"},
    {"т+ысячи", "т+ысяч", "т+ысячам", "т+ысячи", "т+ысячами", "т+ысячах"},
    "т+ысячн",
  },
  {
    Gender::MASCULINE,
    {"милли+он", "милли+она", "милли+ону", "милли+он", "милли+оном", "милли+оне"},
    {"милли+оны", "милли+онов", "милли+онам", "милли+оны", "милли+онами", "милли+онах"},
    "милли+онн",
  },
  {
    Gender::MASCULINE,
    {"милли+ард", "милли+арда", "милли+арду", "милли+ард", "милли+ардом", "милли+арде"},
    {"милли+арды", "милли+ардов", "милли+ардам", "милли+арды", "милли+ардами", "милли+ардах"},
    "милли+ардн",
  },
  {
    Gender::MASCULINE,
    {"трилли+он", "трилли+она", "трилли+ону", "трилли+он", "трилли+оном", "трилли+оне"},
    {"трилли+оны", "трилли+онов", "трилли+онам", "трилли+оны", "трилли+онами", "трилли+онах"},
    "трилли+онн",
  },
}};

// How an ordinal is declined: "п+ервый", "втор+ой", "тр+етий".
enum class OrdinalType { STEM, ENDING, SOFT };

struct OrdinalStem {
  std::string_view stem;
  OrdinalType type;
};

constexpr int kPluralEndings = 3;
// By the type, then by the gender (the plural goes last), then by the case.
constexpr std::array<std::array<Forms, 4>, 3> kOrdinalEndings = {{
  {{
    {"ый", "ого", "ому", "ый", "ым", "ом"},
    {"ая", "ой", "ой", "ую", "ой", "ой"},
    {"ое", "ого", "ому", "ое", "ым", "ом"},
    {"ые", "ых", "ым", "ые", "ыми", "ых"},
  }},
  {{
    {"+ой", "+ого", "+ому", "+ой", "+ым", "+ом"},
    {"+ая", "+ой", "+ой", "+ую", "+ой", "+ой"},
    {"+ое", "+ого", "+ому", "+ое", "+ым", "+ом"},
    {"+ые", "+ых", "+ым", "+ые", "+ыми", "+ых"},
  }},
  {{
    {"ий", "ьего", "ьему", "ий", "ьим", "ьем"},
    {"ья", "ьей", "ьей", "ью", "ьей", "ьей"},
    {"ье", "ьего", "ьему", "ье", "ьим", "ьем"},
    {"ьи", "ьих", "ьим", "ьи", "ьими", "ьих"},
  }},
}};

// By value.
constexpr std::array<OrdinalStem, 20> kOrdinalUnits = {{
  {"нулев", OrdinalType::ENDING},      {"п+ерв", OrdinalType::STEM},        {"втор", OrdinalType::ENDING},
  {"тр+ет", OrdinalType::SOFT},        {"четвёрт", OrdinalType::STEM},      {"п+ят", OrdinalType::STEM},
  {"шест", OrdinalType::ENDING},       {"седьм", OrdinalType::ENDING},      {"восьм", OrdinalType::ENDING},
  {"дев+ят", OrdinalType::STEM},       {"дес+ят", OrdinalType::STEM},       {"од+иннадцат", OrdinalType::STEM},
  {"двен+адцат", OrdinalType::STEM},   {"трин+адцат", OrdinalType::STEM},   {"чет+ырнадцат", OrdinalType::STEM},
  {"пятн+адцат", OrdinalType::STEM},   {"шестн+адцат", OrdinalType::STEM},  {"семн+адцат", OrdinalType::STEM},
  {"восемн+адцат", OrdinalType::STEM}, {"девятн+адцат", OrdinalType::STEM},
}};

// By the tens digit; 0 and 1 are not used.
constexpr std::array<OrdinalStem, 10> kOrdinalTens = {{
  {},
  {},
  {"двадц+ат", OrdinalType::STEM},
  {"тридц+ат", OrdinalType::STEM},
  {"сороков", OrdinalType::ENDING},
  {"пятидес+ят", OrdinalType::STEM},
  {"шестидес+ят", OrdinalType::STEM},
  {"семидес+ят", OrdinalType::STEM},
  {"восьмидес+ят", OrdinalType::STEM},
  {"девян+ост", OrdinalType::STEM},
}};

// By the hundreds digit; 0 is not used.
constexpr std::array<OrdinalStem, 10> kOrdinalHundreds = {{
  {},
  {"с+от", OrdinalType::STEM},
  {"двухс+от", OrdinalType::STEM},
  {"трёхс+от", OrdinalType::STEM},
  {"четырёхс+от", OrdinalType::STEM},
  {"пятис+от", OrdinalType::STEM},
  {"шестис+от", OrdinalType::STEM},
  {"семис+от", OrdinalType::STEM},
  {"восьмис+от", OrdinalType::STEM},
  {"девятис+от", OrdinalType::STEM},
}};

constexpr OrdinalStem kWholeStem = {"ц+ел", OrdinalType::STEM};
// By the number of fraction digits; 0 is not used.
constexpr std::array<OrdinalStem, kMaxFractionDigits + 1> kFractionStems = {{
  {},
  {"дес+ят", OrdinalType::STEM},
  {"с+от", OrdinalType::STEM},
  {"т+ысячн", OrdinalType::STEM},
}};

void AppendWord(std::string& text, std::string_view word) {
  if (!text.empty()) {
    text += ' ';
  }
  text += word;
}

std::string_view UnitWord(int value, int case_index, Gender gender) {
  if (value == 1) {
    return kOne[static_cast<int>(gender)][case_index];
  }
  if (value == 2 && gender == Gender::FEMININE) {
    return kTwoFeminine[case_index];
  }
  return kUnits[value][case_index];
}

// A number from 1 to 999.
void AppendTriple(std::string& text, int value, int case_index, Gender gender) {
  if (value >= 100) {
    AppendWord(text, kHundreds[value / 100][case_index]);
  }
  int rest = value % 100;
  if (rest >= 20) {
    AppendWord(text, kTens[rest / 10][case_index]);
    rest %= 10;
  }
  if (rest > 0) {
    AppendWord(text, UnitWord(rest, case_index, gender));
  }
}

bool IsDirectCase(int case_index) {
  return case_index == kNominative || case_index == kAccusative;
}

// A number with its group noun: "дв+адцать одн+а т+ысяча", "пят+и т+ысяч". A leading one is dropped ("т+ысяча",
// "милли+он"), a one in the middle is not ("два милли+она одн+а т+ысяча").
void AppendGroup(std::string& text, int group, int value, int case_index, Gender gender) {
  if (group == 0) {
    AppendTriple(text, value, case_index, gender);
    return;
  }
  const auto& noun = kGroupNouns[group];
  if (value != 1 || !text.empty()) {
    AppendTriple(text, value, case_index, noun.gender);
  }
  switch (CountFormOf(value)) {
    case CountForm::ONE:
      AppendWord(text, noun.singular[case_index]);
      break;
    case CountForm::FEW:
      AppendWord(text, IsDirectCase(case_index) ? noun.singular[kGenitive] : noun.plural[case_index]);
      break;
    case CountForm::MANY:
      AppendWord(text, IsDirectCase(case_index) ? noun.plural[kGenitive] : noun.plural[case_index]);
      break;
  }
}

std::array<int, kGroupCount> SplitGroups(uint64_t value) {
  if (value > kMaxNumeral) {
    throw std::out_of_range("the number is too large to be read in words: " + std::to_string(value));
  }
  std::array<int, kGroupCount> groups{};
  for (auto& group : groups) {
    group = static_cast<int>(value % 1000);
    value /= 1000;
  }
  return groups;
}

std::string DeclineOrdinal(const OrdinalStem& ordinal, int case_index, int endings) {
  std::string word(ordinal.stem);
  word += kOrdinalEndings[static_cast<int>(ordinal.type)][endings][case_index];
  return word;
}

// A feminine adjective after a number: "одн+а дес+ятая", "две дес+ятых", "пят+и дес+ятым".
std::string CountedFeminine(const OrdinalStem& adjective, uint64_t count, int case_index) {
  if (CountFormOf(count) == CountForm::ONE) {
    return DeclineOrdinal(adjective, case_index, static_cast<int>(Gender::FEMININE));
  }
  return DeclineOrdinal(adjective, IsDirectCase(case_index) ? kGenitive : case_index, kPluralEndings);
}

void AppendWithoutStress(std::string& text, std::string_view word) {
  for (const char symbol : word) {
    if (symbol != '+') {
      text += symbol;
    }
  }
}

// The first part of "двухт+ысячный", "двадцатиоднот+ысячный", "стот+ысячный": the genitive of the number in one word;
// 1, 90 and 100 keep their own forms.
std::string CompoundPrefixOf(int value) {
  std::string prefix;
  if (value >= 100) {
    AppendWithoutStress(prefix, value / 100 == 1 ? std::string_view("сто") : kHundreds[value / 100][kGenitive]);
  }
  int rest = value % 100;
  if (rest >= 20) {
    AppendWithoutStress(prefix, rest / 10 == 9 ? std::string_view("девяносто") : kTens[rest / 10][kGenitive]);
    rest %= 10;
  }
  if (rest == 1) {
    prefix += "одно";
  } else if (rest > 1) {
    AppendWithoutStress(prefix, kUnits[rest][kGenitive]);
  }
  return prefix;
}

} // namespace

CountForm CountFormOf(uint64_t value) {
  const auto last_two = value % 100;
  const auto last = value % 10;
  if (last_two >= 11 && last_two <= 14) {
    return CountForm::MANY;
  }
  if (last == 1) {
    return CountForm::ONE;
  }
  if (last >= 2 && last <= 4) {
    return CountForm::FEW;
  }
  return CountForm::MANY;
}

std::string Cardinal(uint64_t value, GrammaticalCase grammatical_case, Gender gender) {
  const auto groups = SplitGroups(value);
  const auto case_index = static_cast<int>(grammatical_case);
  if (value == 0) {
    return std::string(kZero[case_index]);
  }
  std::string text;
  for (int group = kGroupCount - 1; group >= 0; --group) {
    if (groups[group] != 0) {
      AppendGroup(text, group, groups[group], case_index, gender);
    }
  }
  return text;
}

std::string Ordinal(uint64_t value, GrammaticalCase grammatical_case, Gender gender, bool plural) {
  const auto groups = SplitGroups(value);
  const auto case_index = static_cast<int>(grammatical_case);
  const auto endings = plural ? kPluralEndings : static_cast<int>(gender);
  if (value == 0) {
    return DeclineOrdinal(kOrdinalUnits[0], case_index, endings);
  }
  int last_group = 0;
  while (groups[last_group] == 0) {
    ++last_group;
  }
  // Only the last word is an ordinal, the words before it stay a cardinal in the nominative.
  std::string text;
  for (int group = kGroupCount - 1; group > last_group; --group) {
    if (groups[group] != 0) {
      AppendGroup(text, group, groups[group], kNominative, Gender::MASCULINE);
    }
  }
  const auto last = groups[last_group];
  if (last_group > 0) {
    std::string word = last == 1 ? std::string() : CompoundPrefixOf(last);
    word += DeclineOrdinal({kGroupNouns[last_group].ordinal_stem, OrdinalType::STEM}, case_index, endings);
    AppendWord(text, word);
    return text;
  }
  const auto hundreds = last / 100;
  const auto rest = last % 100;
  if (rest == 0) {
    AppendWord(text, DeclineOrdinal(kOrdinalHundreds[hundreds], case_index, endings));
    return text;
  }
  if (hundreds > 0) {
    AppendWord(text, kHundreds[hundreds][kNominative]);
  }
  if (rest < 20) {
    AppendWord(text, DeclineOrdinal(kOrdinalUnits[rest], case_index, endings));
  } else if (rest % 10 == 0) {
    AppendWord(text, DeclineOrdinal(kOrdinalTens[rest / 10], case_index, endings));
  } else {
    AppendWord(text, kTens[rest / 10][kNominative]);
    AppendWord(text, DeclineOrdinal(kOrdinalUnits[rest % 10], case_index, endings));
  }
  return text;
}

std::string Decimal(uint64_t whole, std::string_view fraction_digits, GrammaticalCase grammatical_case) {
  if (fraction_digits.empty() || fraction_digits.size() > static_cast<size_t>(kMaxFractionDigits)) {
    throw std::invalid_argument(
      "a decimal fraction is read in words with 1 to " + std::to_string(kMaxFractionDigits) + " digits, got '"
      + std::string(fraction_digits) + "'"
    );
  }
  uint64_t numerator = 0;
  for (const char digit : fraction_digits) {
    if (digit < '0' || digit > '9') {
      throw std::invalid_argument("not a digit in a decimal fraction: '" + std::string(fraction_digits) + "'");
    }
    numerator = numerator * 10 + static_cast<uint64_t>(digit - '0');
  }
  const auto case_index = static_cast<int>(grammatical_case);
  std::string text = Cardinal(whole, grammatical_case, Gender::FEMININE);
  AppendWord(text, CountedFeminine(kWholeStem, whole, case_index));
  AppendWord(text, Cardinal(numerator, grammatical_case, Gender::FEMININE));
  AppendWord(text, CountedFeminine(kFractionStems[fraction_digits.size()], numerator, case_index));
  return text;
}

std::string CompoundPrefix(uint64_t value) {
  if (value < 1 || value > 999) {
    throw std::out_of_range("a compound prefix is made of 1 to 999, got " + std::to_string(value));
  }
  return CompoundPrefixOf(static_cast<int>(value));
}

} // namespace giga_edge::vits
