#pragma once

#include "giga_edge/modules/tts/interpreter/backends/vits/text/numerals.h"

#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace giga_edge {
class IDataReader;
} // namespace giga_edge

namespace giga_edge::vits {

// One grammatical reading of a noun form: "часам" is a dative plural.
struct NounReading {
  Gender gender = Gender::MASCULINE;
  bool plural = false;
  GrammaticalCase grammatical_case = GrammaticalCase::NOMINATIVE;
  bool animate = false; // an accusative that is the genitive of an animate noun: "гостя"
};

// How a number before a noun reads as an ordinal: "на 5 этаже" (ALWAYS), "в 2025 году" (OBLIQUE: only in the oblique
// cases, "21 год" stays a cardinal).
enum class OrdinalUse { NONE, ALWAYS, OBLIQUE };

// Forms with stress marks; the first alternative of the data.
struct Noun {
  Gender gender = Gender::MASCULINE;
  OrdinalUse ordinal_use = OrdinalUse::NONE;
  bool label = false;       // a number after it is its name: "лампа 2"
  bool plural_only = false; // "сутки": "одн+и сутки", "дв+ое суток"
  bool time = false;        // "среда": after "в" the accusative, "в сред+у"
  bool animate = false;     // the accusative is the genitive: "встретил одног+о гостя"; from the forms
  std::array<std::string, kCaseCount> singular;
  std::array<std::string, kCaseCount> plural;
  std::string count;             // after 2, 3, 4 in the nominative: "час+а"
  const Noun* subunit = nullptr; // a currency: its hundredth
};

struct Unit {
  const Noun* noun = nullptr;
  std::string tail; // "в час"
};

// An abbreviation of a word: "т. е.", "ул.". The reading is fixed, or a noun declined after a preposition.
struct Abbreviation {
  enum class Condition { ALWAYS, NUMBER, NAME, ADDRESS };
  std::string reading;
  const Noun* noun = nullptr;
  Condition condition = Condition::ALWAYS;
  bool before_name = false; // a name follows it as usual, its dot does not end a sentence: "ул. Ленина"
};

struct NounMatch {
  const Noun* noun = nullptr;
  std::vector<NounReading> readings;
};

// The open word lists of the language: nouns that numbers agree with (nouns.tsv), abbreviations of units (units.tsv)
// and of words (abbreviations.tsv), service words (words.tsv), see the headers of the files.
class Lexicon {
public:
  // Throws std::runtime_error on a malformed file and ReaderException on a missing one.
  Lexicon(IDataReader& reader, const std::string& directory);
  // The maps point into nouns_.
  Lexicon(const Lexicon&) = delete;
  Lexicon& operator=(const Lexicon&) = delete;
  Lexicon(Lexicon&&) = default;
  Lexicon& operator=(Lexicon&&) = default;
  ~Lexicon() = default;

  // A noun by any of its forms; `word` is lower-case, stress marks and 'ё' do not matter. Null if unknown.
  const NounMatch* FindNoun(std::string_view word) const;

  // Case-sensitive, without a final dot. Null if unknown.
  const Unit* FindUnit(std::string_view abbreviation) const;

  // The abbreviations written this way, the first one first; `written` is in lower case, without spaces, with '_'
  // between two words ("в_т.ч."). Null if unknown.
  const std::vector<Abbreviation>* FindAbbreviation(std::string_view written) const;

  // Whether an abbreviation starts with the text, in the form of the key: "т." for "т. е.", "в_т" for "в т. ч.".
  bool IsAbbreviationPrefix(const std::string& written) const {
    return abbreviation_prefixes_.count(written) != 0;
  }

  // The cases a number after the word is in, the most likely first; empty if the word governs nothing (words.tsv).
  const std::vector<GrammaticalCase>& GovernedCases(std::string_view word) const;

  // Whether the lower-case word is in the section of words.tsv: "transparent", "counting", "title", "word"…
  bool IsServiceWord(std::string_view section, std::string_view word) const;

  // A preposition, a conjunction or another word that is never the noun of a number.
  bool IsNotNoun(std::string_view word) const;

  // The cases a number right after a verb, a noun or an adjective is in (government.tsv): "достигает 100" -> genitive.
  const std::vector<GrammaticalCase>& WordGovernedCases(std::string_view word) const;

  // Whether the word ends as a verb form and is not a known other word (government.tsv): "принял", not "бюджет".
  bool LooksLikeVerb(std::string_view word) const;

private:
  std::vector<Noun> nouns_;
  std::unordered_map<std::string, NounMatch> forms_;
  std::unordered_map<std::string, Unit> units_;
  std::unordered_map<std::string, std::vector<Abbreviation>> abbreviations_;
  std::unordered_set<std::string> abbreviation_prefixes_;
  std::unordered_map<std::string, std::vector<GrammaticalCase>> government_;
  std::unordered_map<std::string, std::unordered_set<std::string>> service_words_;
  std::unordered_map<std::string, std::vector<GrammaticalCase>> word_government_;
  std::vector<std::string> verb_endings_;
  std::unordered_set<std::string> not_verbs_;
};

// A word for comparison: no stress marks, 'ё' as 'е'.
std::string LookupKey(std::string_view word);

enum class PartOfSpeech { OTHER, NOUN, ADJECTIVE, VERB };

// One analysis of a lower-case word form by a dictionary of words: "пациента" is a masculine singular noun in the
// genitive and, another analysis, in the animate accusative.
struct WordForm {
  PartOfSpeech part = PartOfSpeech::OTHER;
  NounReading reading;       // of a noun or an adjective; `animate` for the accusative that is the genitive, masculine
                             // in the singular: "пациента", "студенток", not "студентку"
  bool indeclinable = false; // "кофе", "метро"
  bool plural_only = false;  // "сутки"
  bool transitive = false;   // a verb with an object in the accusative: "видит"
};

// The analyses of a word form, empty for an unknown word. Without it the normalizer has its tables and guesses by the
// ending; a morphological dictionary plugs in here.
using WordInfo = std::function<std::vector<WordForm>(std::string_view word)>;

// Throws std::runtime_error unless the text of data is valid UTF-8 with every '+' before a Russian vowel
// (`stress_marks`; a written word may hold another '+').
void CheckSpoken(std::string_view text, bool stress_marks = true);

// Throws std::runtime_error unless a reading is spoken: Russian letters, spaces, '-' and '+', a letter at least.
void CheckReading(std::string_view reading);

// The line without the byte order mark of UTF-8 at its start.
std::string_view WithoutBom(std::string_view line);

} // namespace giga_edge::vits
