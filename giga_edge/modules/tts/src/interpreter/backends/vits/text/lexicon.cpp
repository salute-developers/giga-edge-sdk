#include "giga_edge/modules/tts/interpreter/backends/vits/text/lexicon.h"

#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/utf8.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace giga_edge::vits {

namespace {

constexpr std::string_view kNounsFile = "nouns.tsv";
constexpr std::string_view kUnitsFile = "units.tsv";
constexpr std::string_view kAbbreviationsFile = "abbreviations.tsv";
constexpr std::string_view kWordsFile = "words.tsv";
constexpr std::string_view kGovernmentFile = "government.tsv";
constexpr std::string_view kSections[] = {
  "govern",
  "transparent",
  "nominative_date",
  "weekday",
  "counting",
  "mass",
  "version",
  "garbage",
  "day_part",
  "function",
  "title",
  "word",
  "with_from",
  "person",
  "feminine",
  "distance",
  "persons"
};

// The key of an abbreviation: spaces left out, but a space between two words is '_': "в т. ч." -> "в_т.ч.".
std::string AbbreviationKey(std::string_view text) {
  std::string result;
  for (size_t pos = 0; pos < text.size(); ++pos) {
    if (text[pos] != ' ') {
      result += text[pos];
    } else if (
      !result.empty() && result.back() != '.' && result.back() != '-' && result.back() != '_' && pos + 1 < text.size()
      && text[pos + 1] != ' '
    ) {
      result += '_';
    }
  }
  return result;
}

std::vector<std::string_view> Split(std::string_view text, char separator) {
  std::vector<std::string_view> parts;
  size_t begin = 0;
  while (true) {
    const auto end = text.find(separator, begin);
    parts.push_back(text.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin));
    if (end == std::string_view::npos) {
      return parts;
    }
    begin = end + 1;
  }
}

std::string ReadText(IDataReader& reader, const std::string& path) {
  return data_reader::ToString(data_reader::EnsureNotNull(reader.ReadTransientData(path), path));
}

// The lines of a data file but comments and empty lines, split into columns; errors name the line.
// `line_number` gets the number of the line the handler is called for.
template<typename Handler>
void ForEachLine(std::string_view text, const std::string& path, Handler&& handler, int* line_number = nullptr) {
  int number = 0;
  for (auto line : Split(text, '\n')) {
    ++number;
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    if (number == 1) {
      line = WithoutBom(line);
    }
    if (line.empty() || line.front() == '#') {
      continue;
    }
    try {
      CheckSpoken(line);
      if (line_number != nullptr) {
        *line_number = number;
      }
      handler(Split(line, '\t'));
    } catch (const std::exception& error) {
      throw std::runtime_error(path + ":" + std::to_string(number) + ": " + error.what());
    }
  }
}

// Cases written as letters, the most likely first: "GI".
std::vector<GrammaticalCase> ParseCases(std::string_view letters) {
  constexpr std::string_view case_letters = "NGDAIP";
  std::vector<GrammaticalCase> cases;
  for (const char letter : letters) {
    if (case_letters.find(letter) == std::string_view::npos) {
      throw std::runtime_error("cases are letters of NGDAIP, got '" + std::string(letters) + "'");
    }
    cases.push_back(static_cast<GrammaticalCase>(case_letters.find(letter)));
  }
  if (cases.empty()) {
    throw std::runtime_error("no cases");
  }
  return cases;
}

Gender ParseGender(std::string_view text) {
  if (text == "m") {
    return Gender::MASCULINE;
  }
  if (text == "f") {
    return Gender::FEMININE;
  }
  if (text == "n") {
    return Gender::NEUTER;
  }
  throw std::runtime_error("the gender is m, f or n, got '" + std::string(text) + "'");
}

} // namespace

void CheckSpoken(std::string_view text, bool stress_marks) {
  const std::u32string_view vowels = U"аеёиоуыэюя";
  bool stress = false;
  for (size_t pos = 0; pos < text.size();) {
    CodePoint code_point;
    try {
      code_point = DecodeUtf8(text, pos);
    } catch (const std::invalid_argument&) {
      throw std::runtime_error("invalid UTF-8 in '" + std::string(text) + "'");
    }
    if (stress && vowels.find(code_point.value) == std::u32string_view::npos) {
      throw std::runtime_error("'+' stands before a vowel, see '" + std::string(text) + "'");
    }
    stress = stress_marks && code_point.value == U'+';
    pos += code_point.size;
  }
  if (stress) {
    throw std::runtime_error("'+' stands before a vowel, see '" + std::string(text) + "'");
  }
}

std::string_view WithoutBom(std::string_view line) {
  constexpr std::string_view bom = "\xEF\xBB\xBF";
  return line.substr(0, bom.size()) == bom ? line.substr(bom.size()) : line;
}

// A reading is spoken: Russian letters, spaces, hyphens and stress marks, a letter at least ("foo\t123" is not).
void CheckReading(std::string_view reading) {
  bool letter = false;
  for (size_t pos = 0; pos < reading.size();) {
    const auto code_point = DecodeUtf8(reading, pos);
    const auto value = code_point.value;
    const auto russian = (value >= U'а' && value <= U'я') || (value >= U'А' && value <= U'Я') || value == U'ё'
      || value == U'Ё';
    if (!russian && value != U' ' && value != U'-' && value != U'+') {
      throw std::runtime_error("a reading is Russian letters, spaces, '-' and '+', see '" + std::string(reading) + "'");
    }
    letter = letter || russian;
    pos += code_point.size;
  }
  if (!letter) {
    throw std::runtime_error("a reading without letters: '" + std::string(reading) + "'");
  }
}

std::string LookupKey(std::string_view word) {
  constexpr std::string_view yo = "ё";
  constexpr std::string_view ye = "е";
  std::string key;
  key.reserve(word.size());
  for (size_t i = 0; i < word.size(); ++i) {
    if (word[i] == '+') {
      continue;
    }
    if (word.compare(i, yo.size(), yo) == 0) {
      key += ye;
      i += yo.size() - 1;
      continue;
    }
    key += word[i];
  }
  return key;
}

Lexicon::Lexicon(IDataReader& reader, const std::string& directory) {
  const auto prefix = directory.empty() || directory.back() == '/' ? directory : directory + "/";
  std::vector<std::string> subunits;
  std::vector<int> subunit_lines;
  int line_number = 0;
  // Forms with all their alternatives, by noun.
  std::vector<std::array<std::vector<std::string_view>, 2 * kCaseCount>> alternatives;
  const auto nouns_path = prefix + std::string(kNounsFile);
  // The alternatives point into the text.
  const auto nouns_text = ReadText(reader, nouns_path);
  ForEachLine(
    nouns_text,
    nouns_path,
    [&](const std::vector<std::string_view>& columns) {
      if (columns.size() != 4 && columns.size() != 5) {
        throw std::runtime_error("expected 4 or 5 tab-separated columns, got " + std::to_string(columns.size()));
      }
      Noun noun;
      noun.gender = ParseGender(columns[0]);
      const auto use = columns[1];
      constexpr std::string_view money_prefix = "money:";
      std::string subunit;
      if (use == "ordinal") {
        noun.ordinal_use = OrdinalUse::ALWAYS;
      } else if (use == "year") {
        noun.ordinal_use = OrdinalUse::OBLIQUE;
      } else if (use == "label") {
        noun.label = true;
      } else if (use == "plural") {
        noun.plural_only = true;
      } else if (use == "time") {
        noun.time = true;
      } else if (use.substr(0, money_prefix.size()) == money_prefix && use.size() > money_prefix.size()) {
        subunit = LookupKey(use.substr(money_prefix.size()));
      } else if (use != "-") {
        throw std::runtime_error(
          "the use is '-', 'label', 'plural', 'time', 'ordinal', 'year' or 'money:<noun>', got '" + std::string(use)
          + "'"
        );
      }
      std::array<std::vector<std::string_view>, 2 * kCaseCount> forms;
      for (int number = 0; number < 2; ++number) {
        const auto cells = Split(columns[2 + number], '|');
        if (cells.size() != kCaseCount) {
          throw std::runtime_error("expected " + std::to_string(kCaseCount) + " forms separated by '|'");
        }
        for (int case_index = 0; case_index < kCaseCount; ++case_index) {
          auto& form = forms[number * kCaseCount + case_index];
          form = Split(cells[case_index], '/');
          for (const auto alternative : form) {
            if (alternative.empty()) {
              throw std::runtime_error("an empty form");
            }
          }
          (number == 0 ? noun.singular : noun.plural)[case_index] = std::string(form.front());
        }
      }
      const auto genitive = static_cast<int>(GrammaticalCase::GENITIVE);
      noun.count = columns.size() == 5 ? std::string(columns[4]) : noun.singular[genitive];
      const auto accusative = static_cast<int>(GrammaticalCase::ACCUSATIVE);
      noun.animate = noun.gender == Gender::MASCULINE && noun.singular[accusative] == noun.singular[genitive]
        && noun.singular[accusative] != noun.singular[0];
      if (noun.count.empty()) {
        throw std::runtime_error("an empty count form");
      }
      nouns_.push_back(std::move(noun));
      subunits.push_back(std::move(subunit));
      subunit_lines.push_back(line_number);
      alternatives.push_back(std::move(forms));
    },
    &line_number
  );

  for (size_t index = 0; index < nouns_.size(); ++index) {
    auto& noun = nouns_[index];
    for (int form = 0; form < 2 * kCaseCount; ++form) {
      const auto grammatical_case = static_cast<GrammaticalCase>(form % kCaseCount);
      const auto plural = form >= kCaseCount;
      const auto& forms = plural ? noun.plural : noun.singular;
      // The accusative that is the genitive, not the nominative: "гостя", "гостей", "студенток".
      const auto animate = grammatical_case == GrammaticalCase::ACCUSATIVE
        && forms[static_cast<int>(GrammaticalCase::ACCUSATIVE)] == forms[static_cast<int>(GrammaticalCase::GENITIVE)]
        && forms[static_cast<int>(GrammaticalCase::ACCUSATIVE)] != forms[0];
      const NounReading reading{noun.gender, plural, grammatical_case, animate};
      for (const auto alternative : alternatives[index][form]) {
        auto& match = forms_[LookupKey(alternative)];
        if (match.noun != nullptr && match.noun != &noun) {
          // A form of two nouns: the first one wins.
          continue;
        }
        match.noun = &noun;
        match.readings.push_back(reading);
      }
    }
  }
  for (size_t index = 0; index < nouns_.size(); ++index) {
    if (subunits[index].empty()) {
      continue;
    }
    const auto* subunit = FindNoun(subunits[index]);
    if (subunit == nullptr) {
      throw std::runtime_error(
        nouns_path + ":" + std::to_string(subunit_lines[index]) + ": no noun '" + subunits[index]
        + "' for a currency hundredth"
      );
    }
    nouns_[index].subunit = subunit->noun;
  }

  const auto units_path = prefix + std::string(kUnitsFile);
  ForEachLine(ReadText(reader, units_path), units_path, [&](const std::vector<std::string_view>& columns) {
    if (columns.size() != 2 && columns.size() != 3) {
      throw std::runtime_error("expected 2 or 3 tab-separated columns, got " + std::to_string(columns.size()));
    }
    const auto* match = FindNoun(LookupKey(columns[1]));
    if (match == nullptr || LookupKey(match->noun->singular[0]) != LookupKey(columns[1])) {
      throw std::runtime_error("no noun with the nominative singular '" + std::string(columns[1]) + "' in nouns.tsv");
    }
    if (columns[0].empty()) {
      throw std::runtime_error("an empty abbreviation");
    }
    Unit unit{match->noun, columns.size() == 3 ? std::string(columns[2]) : std::string()};
    if (!units_.emplace(std::string(columns[0]), std::move(unit)).second) {
      throw std::runtime_error("the abbreviation '" + std::string(columns[0]) + "' is given twice");
    }
  });

  const auto abbreviations_path = prefix + std::string(kAbbreviationsFile);
  ForEachLine(
    ReadText(reader, abbreviations_path), abbreviations_path, [&](const std::vector<std::string_view>& columns) {
      if (columns.size() != 2 && columns.size() != 3) {
        throw std::runtime_error("expected 2 or 3 tab-separated columns, got " + std::to_string(columns.size()));
      }
      if (columns[0].empty() || columns[1].empty()) {
        throw std::runtime_error("an empty abbreviation or reading");
      }
      Abbreviation abbreviation;
      constexpr std::string_view noun_prefix = "noun:";
      if (columns[1].substr(0, noun_prefix.size()) == noun_prefix) {
        const auto lemma = LookupKey(columns[1].substr(noun_prefix.size()));
        const auto* match = FindNoun(lemma);
        if (match == nullptr || LookupKey(match->noun->singular[0]) != lemma) {
          throw std::runtime_error("no noun with the nominative singular '" + lemma + "' in nouns.tsv");
        }
        abbreviation.noun = match->noun;
      } else {
        CheckReading(columns[1]);
        abbreviation.reading = std::string(columns[1]);
      }
      const auto condition = columns.size() == 3 ? columns[2] : std::string_view("-");
      if (condition == "number") {
        abbreviation.condition = Abbreviation::Condition::NUMBER;
      } else if (condition == "name") {
        abbreviation.condition = Abbreviation::Condition::NAME;
        abbreviation.before_name = true;
      } else if (condition == "before-name") {
        abbreviation.before_name = true;
      } else if (condition == "address") {
        abbreviation.condition = Abbreviation::Condition::ADDRESS;
      } else if (condition != "-") {
        throw std::runtime_error(
          "the condition is 'number', 'name', 'before-name' or 'address', got '" + std::string(condition) + "'"
        );
      }
      const auto key = AbbreviationKey(columns[0]);
      for (const auto& before : abbreviations_[key]) {
        if (before.condition == abbreviation.condition && before.before_name == abbreviation.before_name) {
          throw std::runtime_error(
            "the abbreviation '" + std::string(columns[0]) + "' is given twice with this condition"
          );
        }
      }
      for (size_t size = 1; size <= key.size(); ++size) {
        abbreviation_prefixes_.insert(key.substr(0, size));
      }
      abbreviations_[key].push_back(std::move(abbreviation));
    }
  );

  const auto words_path = prefix + std::string(kWordsFile);
  ForEachLine(ReadText(reader, words_path), words_path, [&](const std::vector<std::string_view>& columns) {
    const auto section = columns.empty() ? std::string_view() : columns[0];
    if (std::find(std::begin(kSections), std::end(kSections), section) == std::end(kSections)) {
      throw std::runtime_error("unknown section '" + std::string(section) + "'");
    }
    const auto govern = section == "govern";
    if (columns.size() != (govern ? 3U : 2U) || columns[1].empty()) {
      throw std::runtime_error(
        govern ? "expected the section, the word and the cases" : "expected the section and the word"
      );
    }
    if (govern) {
      if (!government_.emplace(std::string(columns[1]), ParseCases(columns[2])).second) {
        throw std::runtime_error("the word is given twice: '" + std::string(columns[1]) + "'");
      }
      return;
    }
    service_words_[std::string(section)].insert(std::string(columns[1]));
  });

  const auto government_path = prefix + std::string(kGovernmentFile);
  ForEachLine(ReadText(reader, government_path), government_path, [&](const std::vector<std::string_view>& columns) {
    if (columns.size() != 3) {
      throw std::runtime_error("expected the kind, the cases and the forms");
    }
    const auto kind = columns[0];
    const auto not_verb = kind == "not_verb";
    if (kind != "verb" && kind != "noun" && kind != "adjective" && kind != "verb_ending" && !not_verb) {
      throw std::runtime_error(
        "the kind is 'verb', 'noun', 'adjective', 'verb_ending' or 'not_verb', got '" + std::string(kind) + "'"
      );
    }
    if (not_verb ? columns[1] != "-" : columns[1] != "A" && kind == "verb_ending") {
      throw std::runtime_error(not_verb ? "the cases of 'not_verb' are '-'" : "the cases of 'verb_ending' are 'A'");
    }
    const auto cases = not_verb ? std::vector<GrammaticalCase>() : ParseCases(columns[1]);
    for (const auto form : Split(columns[2], ',')) {
      if (form.empty()) {
        throw std::runtime_error("an empty form");
      }
      if (kind == "verb_ending") {
        verb_endings_.push_back(LookupKey(form));
      } else if (not_verb) {
        not_verbs_.insert(LookupKey(form));
      } else if (
        const auto [found, added] = word_government_.emplace(LookupKey(form), cases); !added && found->second != cases
      ) {
        throw std::runtime_error("the form '" + std::string(form) + "' is given with other cases before");
      }
    }
  });
}

bool Lexicon::LooksLikeVerb(std::string_view word) const {
  const auto key = LookupKey(word);
  return not_verbs_.count(key) == 0
    && std::any_of(verb_endings_.begin(), verb_endings_.end(), [&key](const std::string& ending) {
           return key.size() > ending.size() && key.compare(key.size() - ending.size(), ending.size(), ending) == 0;
         });
}

const std::vector<GrammaticalCase>& Lexicon::WordGovernedCases(std::string_view word) const {
  static const std::vector<GrammaticalCase> none;
  const auto found = word_government_.find(LookupKey(word));
  return found == word_government_.end() ? none : found->second;
}

const std::vector<GrammaticalCase>& Lexicon::GovernedCases(std::string_view word) const {
  static const std::vector<GrammaticalCase> none;
  const auto found = government_.find(std::string(word));
  return found == government_.end() ? none : found->second;
}

bool Lexicon::IsServiceWord(std::string_view section, std::string_view word) const {
  const auto found = service_words_.find(std::string(section));
  return found != service_words_.end() && found->second.count(std::string(word)) != 0;
}

bool Lexicon::IsNotNoun(std::string_view word) const {
  return !GovernedCases(word).empty() || IsServiceWord("function", word);
}

const std::vector<Abbreviation>* Lexicon::FindAbbreviation(std::string_view written) const {
  const auto found = abbreviations_.find(std::string(written));
  return found == abbreviations_.end() ? nullptr : &found->second;
}

const NounMatch* Lexicon::FindNoun(std::string_view word) const {
  const auto found = forms_.find(LookupKey(word));
  return found == forms_.end() ? nullptr : &found->second;
}

const Unit* Lexicon::FindUnit(std::string_view abbreviation) const {
  const auto found = units_.find(std::string(abbreviation));
  return found == units_.end() ? nullptr : &found->second;
}

} // namespace giga_edge::vits
