#include "giga_edge/modules/tts/interpreter/backends/vits/text/agreement.h"

#include "giga_edge/modules/tts/interpreter/backends/vits/utf8.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace giga_edge::vits {

namespace {

using Case = GrammaticalCase;

constexpr auto kNom = Case::NOMINATIVE;
constexpr auto kGen = Case::GENITIVE;
constexpr auto kDat = Case::DATIVE;
constexpr auto kAcc = Case::ACCUSATIVE;
constexpr auto kIns = Case::INSTRUMENTAL;
constexpr auto kPre = Case::PREPOSITIONAL;
constexpr auto kMasc = Gender::MASCULINE;
constexpr auto kFem = Gender::FEMININE;
constexpr auto kNeut = Gender::NEUTER;

constexpr std::string_view kAdjectiveEndings[] = {
  "ый",
  "ая",
  "яя",
  "ое",
  "ее",
  "ые",
  "ых",
  "их",
  "ого",
  "его",
  "ому",
  "ему",
  "ую",
  "юю",
  "ыми",
  "ими",
  "ым",
  "им",
};

bool EndsWith(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

size_t CodePointCount(std::string_view text) {
  size_t count = 0;
  for (size_t pos = 0; pos < text.size(); ++pos) {
    count += IsUtf8Boundary(text, pos) ? 1 : 0;
  }
  return count;
}

bool IsRoundThousands(uint64_t value) {
  constexpr uint64_t thousand = 1000;
  return value >= thousand && value % thousand == 0;
}

bool IsDirect(Case grammatical_case) {
  return grammatical_case == kNom || grammatical_case == kAcc;
}

// Readings of a noun form by its ending, the longest ending first.
struct EndingReadings {
  std::string_view ending;
  std::vector<NounReading> readings;
};

const std::vector<EndingReadings>& EndingTable() {
  static const std::vector<EndingReadings> table = {
    {"ами", {{kMasc, true, kIns}}},
    {"ями", {{kMasc, true, kIns}}},
    {"ам", {{kMasc, true, kDat}}},
    {"ям", {{kMasc, true, kDat}}},
    {"ах", {{kMasc, true, kPre}}},
    {"ях", {{kMasc, true, kPre}}},
    {"ов", {{kMasc, true, kGen}, {kMasc, true, kAcc, true}}},
    {"ев", {{kMasc, true, kGen}, {kMasc, true, kAcc, true}}},
    {"ёв", {{kMasc, true, kGen}, {kMasc, true, kAcc, true}}},
    {"ей", {{kMasc, true, kGen}, {kMasc, true, kAcc, true}, {kFem, false, kIns}}},
    {"ий", {{kMasc, false, kNom}, {kMasc, false, kAcc}, {kMasc, true, kGen}}},
    {"ой", {{kMasc, false, kNom}, {kMasc, false, kAcc}, {kFem, false, kIns}}},
    {"ом", {{kMasc, false, kIns}}},
    {"ем", {{kMasc, false, kIns}}},
    {"ём", {{kMasc, false, kIns}}},
    {"ие", {{kNeut, false, kNom}, {kNeut, false, kAcc}, {kNeut, false, kPre}}},
    {"ье", {{kNeut, false, kNom}, {kNeut, false, kAcc}}},
    {"а", {{kFem, false, kNom}, {kMasc, false, kGen}, {kNeut, false, kGen}, {kMasc, false, kAcc, true}}},
    {"я", {{kFem, false, kNom}, {kMasc, false, kGen}, {kNeut, false, kGen}, {kMasc, false, kAcc, true}}},
    {"ы", {{kFem, false, kGen}}},
    {"и", {{kFem, false, kGen}}},
    {"у", {{kFem, false, kAcc}, {kMasc, false, kDat}}},
    {"ю", {{kFem, false, kAcc}, {kMasc, false, kDat}}},
    {"е", {{kFem, false, kDat}, {kFem, false, kPre}, {kMasc, false, kPre}}},
    {"о", {{kNeut, false, kNom}, {kNeut, false, kAcc}}},
    // The genitive plural of "цапля", "туфля" too: "2 цапель".
    {"ель",
     {{kMasc, false, kNom},
      {kMasc, false, kAcc},
      {kFem, false, kNom},
      {kFem, false, kAcc},
      {kFem, true, kGen},
      {kFem, true, kAcc, true}}},
    {"ь", {{kMasc, false, kNom}, {kMasc, false, kAcc}, {kFem, false, kNom}, {kFem, false, kAcc}}},
    {"й", {{kMasc, false, kNom}, {kMasc, false, kAcc}}},
  };
  return table;
}

bool IsCyrillicWord(std::string_view word) {
  for (size_t pos = 0; pos < word.size();) {
    const auto code_point = DecodeUtf8(word, pos);
    if (!((code_point.value >= U'а' && code_point.value <= U'я') || code_point.value == U'ё')) {
      return false;
    }
    pos += code_point.size;
  }
  return !word.empty();
}

// A one-letter-or-so ending that marks a cardinal: "2-х", "5-ти", "3-мя".
bool IsCardinalSuffix(std::string_view suffix) {
  constexpr std::string_view cardinal_suffixes[] = {"х", "ух", "ёх", "ех", "и", "ти", "ми", "мя", "ью", "умя"};
  return std::find(std::begin(cardinal_suffixes), std::end(cardinal_suffixes), suffix) != std::end(cardinal_suffixes);
}

// Keeps the readings that pass, unless none does.
template<typename Predicate>
void Narrow(std::vector<NumeralReading>& readings, Predicate&& predicate) {
  std::vector<NumeralReading> kept;
  std::copy_if(readings.begin(), readings.end(), std::back_inserter(kept), predicate);
  if (!kept.empty()) {
    readings = std::move(kept);
  }
}

} // namespace

bool Fits(uint64_t value, const NumeralReading& numeral, const NounReading& noun) {
  // The accusative of the animate is the genitive: "одног+о пациента", "двух друзей"; five and more keep the form of
  // the inanimate ("пять друзей").
  if (numeral.animate) {
    if (!noun.animate) {
      return false;
    }
    if (CountFormOf(value) == CountForm::ONE) {
      return !noun.plural; // a singular animate accusative is masculine, as the reading
    }
    // "двух друзей"; "двадцать два студента" agrees as the inanimate, with the genitive singular.
    return CountFormOf(value) == CountForm::FEW && noun.plural;
  }
  if (noun.animate) {
    return false;
  }
  if (numeral.ordinal) {
    return noun.grammatical_case == numeral.grammatical_case && noun.plural == numeral.plural
      && (noun.plural || noun.gender == numeral.gender);
  }
  // "к двум т+ысячам рубл+ей": after round thousands the noun is a genitive plural in every case.
  if (IsRoundThousands(value)) {
    return noun.plural && noun.grammatical_case == kGen;
  }
  const auto direct = IsDirect(numeral.grammatical_case);
  switch (CountFormOf(value)) {
    case CountForm::ONE:
      return !noun.plural && noun.grammatical_case == numeral.grammatical_case && noun.gender == numeral.gender;
    case CountForm::FEW:
      if (direct) {
        return !noun.plural && noun.grammatical_case == kGen && noun.gender == numeral.gender;
      }
      return noun.plural && noun.grammatical_case == numeral.grammatical_case;
    case CountForm::MANY:
      return noun.plural && noun.grammatical_case == (direct ? kGen : numeral.grammatical_case);
  }
  return false;
}

bool LooksLikeAdjective(std::string_view word) {
  if (CodePointCount(word) < 4) {
    return false;
  }
  // A reflexive participle: "гнездящимся", "оставшихся".
  constexpr std::string_view reflexive_ending = "ся";
  if (
    EndsWith(word, reflexive_ending)
    && (word.find("щ") != std::string_view::npos || word.find("вш") != std::string_view::npos)
  ) {
    word.remove_suffix(reflexive_ending.size());
  }
  return std::any_of(std::begin(kAdjectiveEndings), std::end(kAdjectiveEndings), [word](std::string_view ending) {
    return EndsWith(word, ending);
  });
}

std::vector<NounReading> GuessNounReadings(std::string_view word) {
  if (CodePointCount(word) < 3 || !IsCyrillicWord(word) || LooksLikeAdjective(word)) {
    return {};
  }
  // Verbs: "подъедет", "горит", "включится".
  constexpr std::string_view verb_endings[] = {"ет", "ит", "ут", "ют", "ешь", "ишь", "тся", "ться", "ся", "сь"};
  if (std::any_of(std::begin(verb_endings), std::end(verb_endings), [word](std::string_view ending) {
        return EndsWith(word, ending);
      })) {
    return {};
  }
  for (const auto& entry : EndingTable()) {
    if (EndsWith(word, entry.ending)) {
      return entry.readings;
    }
  }
  constexpr std::string_view vowels[] = {"а", "е", "ё", "и", "о", "у", "ы", "э", "ю", "я", "ъ"};
  if (std::any_of(std::begin(vowels), std::end(vowels), [word](std::string_view vowel) {
        return EndsWith(word, vowel);
      })) {
    return {};
  }
  // A consonant: "вопрос", "ламп".
  return {{kMasc, false, kNom}, {kMasc, false, kAcc}, {kMasc, true, kGen}};
}

std::vector<GrammaticalCase> AdjectiveCases(std::string_view word) {
  if (!LooksLikeAdjective(word)) {
    return {};
  }
  if (EndsWith(word, "ся")) {
    word.remove_suffix(std::string_view("ся").size());
  }
  const auto ends = [word](std::string_view first, std::string_view second) {
    return EndsWith(word, first) || EndsWith(word, second);
  };
  if (ends("ыми", "ими")) {
    return {kIns};
  }
  if (ends("ым", "им")) {
    return {kDat, kIns};
  }
  if (ends("ому", "ему")) {
    return {kDat};
  }
  if (ends("ого", "его")) {
    return {kGen, kAcc};
  }
  if (ends("ом", "ем")) {
    return {kPre};
  }
  if (ends("ых", "их")) {
    return {kGen, kAcc, kPre};
  }
  if (ends("ой", "ей")) {
    return {kNom, kAcc, kGen, kDat, kIns, kPre};
  }
  if (ends("ую", "юю")) {
    return {kAcc};
  }
  if (ends("ая", "яя")) {
    return {kNom};
  }
  return {kNom, kAcc}; // "-ые", "-ое", "-ый"
}

std::vector<GrammaticalCase> ObliqueAdjectiveCases(std::string_view word) {
  auto cases = AdjectiveCases(word);
  const auto direct = std::any_of(cases.begin(), cases.end(), [](GrammaticalCase grammatical_case) {
    return grammatical_case == kNom || grammatical_case == kAcc || grammatical_case == kGen;
  });
  return direct ? std::vector<GrammaticalCase>() : cases;
}

std::vector<NounReading> ObliqueAdjectiveReadings(std::string_view word) {
  std::vector<NounReading> readings;
  for (const auto grammatical_case : ObliqueAdjectiveCases(word)) {
    // "-ыми", "-ым" (the plural; "-ым" the singular instrumental too), "-ому", "-ом".
    const auto plural_ending = EndsWith(word, "ми") || EndsWith(word, "мся") || EndsWith(word, "ым")
      || EndsWith(word, "им");
    if (plural_ending) {
      readings.push_back({kMasc, true, grammatical_case});
    }
    if (!EndsWith(word, "ми") && (!plural_ending || grammatical_case == kIns)) {
      readings.push_back({kMasc, false, grammatical_case});
    }
  }
  return readings;
}

std::vector<NounReading> SubstantiveReadings(std::string_view word) {
  if (!LooksLikeAdjective(word)) {
    return {};
  }
  if (EndsWith(word, "ого") || EndsWith(word, "его")) {
    return {{kMasc, false, kGen}, {kMasc, false, kAcc, true}};
  }
  if (EndsWith(word, "ых") || EndsWith(word, "их")) {
    return {{kMasc, true, kGen}, {kMasc, true, kAcc, true}};
  }
  return {};
}

std::string ReadNumber(uint64_t value, const NumeralReading& reading) {
  if (reading.ordinal) {
    return Ordinal(value, reading.grammatical_case, reading.gender, reading.plural);
  }
  if (reading.animate) {
    // "одног+о": only the last word of "двадцать один" changes; "двух" is the whole number.
    if (CountFormOf(value) == CountForm::FEW) {
      return Cardinal(value, kGen, reading.gender);
    }
    auto number = Cardinal(value, kAcc, reading.gender);
    const auto last = number.rfind(' ') == std::string::npos ? 0 : number.rfind(' ') + 1;
    return number.substr(0, last) + Cardinal(1, kGen, reading.gender);
  }
  return Cardinal(value, reading.grammatical_case, reading.gender);
}

NumeralReading Agree(uint64_t value, const NumberContext& context) {
  std::vector<NumeralReading> readings;
  for (const bool ordinal : {false, true}) {
    for (int case_index = 0; case_index < kCaseCount; ++case_index) {
      for (const auto gender : {kMasc, kFem, kNeut}) {
        for (const bool plural : {false, true}) {
          if (!ordinal && plural) {
            continue;
          }
          readings.push_back({ordinal, static_cast<Case>(case_index), gender, plural});
          if (!ordinal && static_cast<Case>(case_index) == kAcc && gender == kMasc) {
            readings.push_back({false, kAcc, kMasc, false, true});
          }
        }
      }
    }
  }

  bool by_suffix = false;
  if (!context.suffix.empty()) {
    const auto suffix = LookupKey(context.suffix);
    const auto cardinal_suffix = IsCardinalSuffix(suffix);
    std::vector<NumeralReading> matching;
    for (const bool preferred : {true, false}) {
      for (const auto& reading : readings) {
        if (
          (reading.ordinal != cardinal_suffix || !preferred) && EndsWith(LookupKey(ReadNumber(value, reading)), suffix)
        ) {
          matching.push_back(reading);
        }
      }
      if (!matching.empty()) {
        break;
      }
    }
    if (!matching.empty()) {
      readings = std::move(matching);
      by_suffix = true;
    }
  }
  if (!by_suffix) {
    // A known noun that no cardinal agrees with takes an ordinal: "на 3 странице", "по 19 километр"; a guess may be
    // wrong ("в 5 утра").
    const auto fits_noun = [&](const NumeralReading& reading) {
      return std::any_of(context.noun.begin(), context.noun.end(), [&](const NounReading& noun) {
        return Fits(value, reading, noun);
      });
    };
    const auto ordinal_only = context.ordinal_use == OrdinalUse::NONE && !context.noun_guessed
      && std::none_of(readings.begin(),
                      readings.end(),
                      [&](const NumeralReading& reading) {
                        return !reading.ordinal && fits_noun(reading);
                      })
      && std::any_of(readings.begin(), readings.end(), [&](const NumeralReading& reading) {
                                return reading.ordinal && !reading.plural && fits_noun(reading);
                              });
    readings.erase(
      std::remove_if(
        readings.begin(),
        readings.end(),
        [&context, ordinal_only](const NumeralReading& reading) {
          return reading.plural || (reading.ordinal && context.ordinal_use == OrdinalUse::NONE && !ordinal_only);
        }
      ),
      readings.end()
    );
  }
  // Each source gives its weight to the readings it agrees with, the readings of the highest weight stay: a noun of the
  // lexicon outweighs a preposition, a preposition a noun guessed by the ending ("к 3 подъедет такси" is "к трём"), the
  // guess a governing verb, noun or adjective, which only chooses among the forms of the noun: "принял 21 пациента",
  // but "понравились 2 фильма". A suffix has decided before.
  constexpr int known_noun_weight = 5;
  constexpr int preposition_weight = 4;
  constexpr int guessed_noun_weight = 2;
  constexpr int governor_weight = 1;
  const auto noun_weight = context.noun_guessed ? guessed_noun_weight : known_noun_weight;
  const auto governed_weight = context.governed_weak ? governor_weight : preposition_weight;
  const auto weight = [&](const NumeralReading& reading) {
    auto total = 0;
    if (std::any_of(context.noun.begin(), context.noun.end(), [&](const NounReading& noun) {
          return Fits(value, reading, noun);
        })) {
      total += noun_weight;
    }
    if (
      std::find(context.governed.begin(), context.governed.end(), reading.grammatical_case) != context.governed.end()
    ) {
      total += governed_weight;
    }
    return total;
  };
  auto best_weight = 0;
  for (const auto& reading : readings) {
    best_weight = std::max(best_weight, weight(reading));
  }
  Narrow(readings, [&](const NumeralReading& reading) {
    return weight(reading) == best_weight;
  });
  if (!by_suffix) {
    const auto has = [&readings](auto predicate) {
      return std::any_of(readings.begin(), readings.end(), predicate);
    };
    bool ordinal = false;
    if (context.ordinal_use == OrdinalUse::ALWAYS) {
      // "в 21 веке", "на 1 этаже", but "3 этаж+а", "5 этаж+ей": a counted form keeps the cardinal.
      ordinal = has([](const NumeralReading& reading) {
                  return reading.ordinal;
                })
        && !(CountFormOf(value) != CountForm::ONE && has([](const NumeralReading& reading) {
                  return !reading.ordinal;
                }));
    } else if (context.ordinal_use == OrdinalUse::OBLIQUE) {
      // A calendar year ("наступил 2021 год", "до 1999 года") or "в 21 году"; an age or a count stays a cardinal
      // ("21 год", "для 21 года").
      ordinal = has([value](const NumeralReading& reading) {
                  return reading.ordinal && (value >= kMinCalendarYear || reading.grammatical_case == kPre);
                })
        || !has([](const NumeralReading& reading) {
                  return !reading.ordinal;
                });
    }
    Narrow(readings, [ordinal](const NumeralReading& reading) {
      return reading.ordinal == ordinal;
    });
  }
  // The most likely of the rest: the case the governing word names first, then the direct cases; masculine; singular.
  const auto rank = [&context](const NumeralReading& reading) {
    constexpr std::array<Case, kCaseCount> case_order = {kNom, kAcc, kGen, kPre, kDat, kIns};
    const auto governed = std::find(context.governed.begin(), context.governed.end(), reading.grammatical_case);
    return std::make_tuple(
      governed - context.governed.begin(),
      std::find(case_order.begin(), case_order.end(), reading.grammatical_case) - case_order.begin(),
      reading.plural,
      static_cast<int>(reading.gender)
    );
  };
  return *std::min_element(readings.begin(), readings.end(), [&rank](const auto& left, const auto& right) {
    return rank(left) < rank(right);
  });
}

std::string CountedNoun(const Noun& noun, uint64_t value, const NumeralReading& reading) {
  const auto case_index = static_cast<int>(reading.grammatical_case);
  if (reading.ordinal) {
    return reading.plural ? noun.plural[case_index] : noun.singular[case_index];
  }
  if (IsRoundThousands(value)) {
    return noun.plural[static_cast<int>(kGen)];
  }
  const auto direct = IsDirect(reading.grammatical_case);
  switch (CountFormOf(value)) {
    case CountForm::ONE:
      return noun.singular[case_index];
    case CountForm::FEW:
      return direct ? noun.count : noun.plural[case_index];
    case CountForm::MANY:
      return direct ? noun.plural[static_cast<int>(kGen)] : noun.plural[case_index];
  }
  return noun.plural[case_index];
}

const std::string& NounAfterDecimal(const Noun& noun) {
  return noun.singular[static_cast<int>(kGen)];
}

} // namespace giga_edge::vits
