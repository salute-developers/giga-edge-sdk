#pragma once

#include "giga_edge/modules/tts/interpreter/backends/vits/text/lexicon.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/numerals.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace giga_edge::vits {

// A year from it on is a calendar year before "год": "наступил 2021 год" is an ordinal, "21 год" a count.
inline constexpr uint64_t kMinCalendarYear = 1000;

// How a number is read.
struct NumeralReading {
  bool ordinal = false;
  GrammaticalCase grammatical_case = GrammaticalCase::NOMINATIVE;
  Gender gender = Gender::MASCULINE;
  bool plural = false;  // an ordinal: "девян+остые"
  bool animate = false; // the accusative of the animate: "одног+о пациента", "двух друзей"
};

// What the words around a number tell about its form.
struct NumberContext {
  // The cases the word on the left puts the number in, the most likely first: "к" -> dative; "в" -> accusative,
  // prepositional. Empty: no such word.
  std::vector<GrammaticalCase> governed;
  // The readings of the noun on the right; empty: no noun.
  std::vector<NounReading> noun;
  // The readings are guessed by the ending: the governing word decides first.
  bool noun_guessed = false;
  // The governing word is a verb, a noun or an adjective, not a preposition: weaker than any noun.
  bool governed_weak = false;
  OrdinalUse ordinal_use = OrdinalUse::NONE;
  // A written ending: "го" of "2-го". Empty: none.
  std::string suffix;
};

// The readings of an unknown noun by its ending, as a form after a number; `word` is lower-case. Empty for short
// words, adjectives, verbs and words of other scripts; prepositions and other service words are for the caller to
// leave out (Lexicon::IsNotNoun).
std::vector<NounReading> GuessNounReadings(std::string_view word);

// The cases an adjective is in by its ending: "новых" - genitive, accusative, prepositional.
std::vector<GrammaticalCase> AdjectiveCases(std::string_view word);

// The cases an adjective in an oblique case is in by its ending: "учащимся" - dative or instrumental; empty for an
// ending of a direct case or the genitive ("больших", "новые"), which agree after 2-4 with a noun of another form.
std::vector<GrammaticalCase> ObliqueAdjectiveCases(std::string_view word);

// The readings of an adjective in an oblique case that stands for its noun: "назначили 137 954 учащимся".
std::vector<NounReading> ObliqueAdjectiveReadings(std::string_view word);

// The readings of an adjective that stands for a noun of persons after a number: "осмотрел 21 больного",
// "разбудил 3 часовых"; none for other endings.
std::vector<NounReading> SubstantiveReadings(std::string_view word);

// "новых", "первое": an adjective between a number and its noun.
bool LooksLikeAdjective(std::string_view word);

// Whether a number read this way can stand before the noun form: "две" before "минуты", not before "минут".
bool Fits(uint64_t value, const NumeralReading& numeral, const NounReading& noun);

// The reading of an integer in the context: the suffix is obeyed if it fits a reading, the noun and the governing word
// if a reading fits them; without either the nominative.
NumeralReading Agree(uint64_t value, const NumberContext& context);

// Cardinal or ordinal in words.
std::string ReadNumber(uint64_t value, const NumeralReading& reading);

// The form of a noun counted by the number read this way: "пять килом+етров", "двух килом+етров".
std::string CountedNoun(const Noun& noun, uint64_t value, const NumeralReading& reading);

// The form of a noun after a decimal fraction: the genitive singular.
const std::string& NounAfterDecimal(const Noun& noun);

} // namespace giga_edge::vits
