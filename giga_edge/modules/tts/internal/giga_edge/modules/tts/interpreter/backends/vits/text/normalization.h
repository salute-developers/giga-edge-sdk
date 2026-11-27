#pragma once

// The parts of the text normalizer shared by its files: tokens, helpers and the normalization of one text.

#include "giga_edge/modules/tts/interpreter/backends/vits/encoding.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/agreement.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/latin.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/morph_dict.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/normalizer.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/numerals.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/sentence_splitter.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/utf8.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace giga_edge::vits::normalization {

using Case = GrammaticalCase;

inline constexpr auto kNom = Case::NOMINATIVE;
inline constexpr auto kGen = Case::GENITIVE;

// "Пётр I", "Глава IV": a Roman numeral after a capitalized word is read up to this value.
inline constexpr uint64_t kMaxRomanAfterName = 30;

// The longest integer read in words.
inline constexpr size_t kMaxNumeralDigits = 15;
static_assert(kMaxNumeral == 999'999'999'999'999ULL, "kMaxNumeralDigits digits are always in range");

inline constexpr std::array<std::string_view, 12> kMonths = {
  "янв+аря",
  "февр+аля",
  "м+арта",
  "апр+еля",
  "м+ая",
  "и+юня",
  "и+юля",
  "+августа",
  "сентябр+я",
  "октябр+я",
  "ноябр+я",
  "декабр+я",
};

// By the month: "янв.", "сент.".
inline const std::array<std::vector<std::string_view>, 12> kMonthAbbreviations = {{
  {"янв"},
  {"фев", "февр"},
  {"мар"},
  {"апр"},
  {},
  {"июн"},
  {"июл"},
  {"авг"},
  {"сен", "сент"},
  {"окт"},
  {"ноя", "нояб"},
  {"дек"},
}};

inline bool IsSpace(char32_t ch) {
  return ch == U' ' || (ch >= U'\t' && ch <= U'\r') || ch == 0xA0 || (ch >= 0x2000 && ch <= 0x200B) || ch == 0x202F
    || ch == 0x205F || ch == 0x3000 || ch == 0xFEFF;
}

inline char32_t ToLower(char32_t ch) {
  if ((ch >= U'А' && ch <= U'Я') || (ch >= U'A' && ch <= U'Z')) {
    return ch + (U'а' - U'А');
  }
  return ch == U'Ё' ? U'ё' : ch;
}

inline bool IsCyrillicVowel(char32_t ch) {
  constexpr std::u32string_view vowels = U"аеёиоуыэюяАЕЁИОУЫЭЮЯ";
  return vowels.find(ch) != std::u32string_view::npos;
}

inline std::optional<CodePoint> Decode(std::string_view text, size_t pos) {
  try {
    return DecodeUtf8(text, pos);
  } catch (const std::invalid_argument&) {
    return std::nullopt;
  }
}

// A Latin letter with a diacritic: Latin-1 Supplement and Latin Extended-A and B ("å", "é", "š", "ł").
inline bool IsLatinDiacriticLetter(char32_t ch) {
  constexpr char32_t first_value = 0xC0;
  constexpr char32_t last_value = 0x24F;
  constexpr char32_t times = 0xD7;
  constexpr char32_t divide = 0xF7;
  return ch >= first_value && ch <= last_value && ch != times && ch != divide;
}

// A combining diacritical mark: "e" + U+0301.
inline bool IsCombiningMark(char32_t ch) {
  constexpr char32_t first_value = 0x300;
  constexpr char32_t last_value = 0x36F;
  return ch >= first_value && ch <= last_value;
}

// A letter of a Latin word, with a diacritic too.
inline bool IsLatinLetter(char32_t ch) {
  return (ch >= U'a' && ch <= U'z') || (ch >= U'A' && ch <= U'Z') || IsLatinDiacriticLetter(ch);
}

// A letter, or a stress mark before a vowel.
inline bool IsWordAt(std::string_view text, size_t pos) {
  const auto code_point = Decode(text, pos);
  if (!code_point) {
    return false;
  }
  if (code_point->value == U'+') {
    const auto next = pos + 1 < text.size() ? Decode(text, pos + 1) : std::nullopt;
    return next && IsCyrillicVowel(next->value);
  }
  return IsLetter(code_point->value) || IsLatinDiacriticLetter(code_point->value);
}

inline bool HasLowerCyrillic(std::string_view text) {
  for (size_t pos = 0; pos < text.size();) {
    const auto code_point = DecodeUtf8(text, pos);
    if ((code_point.value >= U'а' && code_point.value <= U'я') || code_point.value == U'ё') {
      return true;
    }
    pos += code_point.size;
  }
  return false;
}

// A Russian word with Latin letters in lower case: the Latin letters that look like Russian ones become those, others
// are dropped if there is any.
// `always`: the look-alikes of a single letter in Russian text ("c 5 до 7"), else only in a word with a Cyrillic letter.
inline std::string RussianLetters(std::string_view word, bool always = false) {
  constexpr std::string_view latin_list = "aceopxykABCEHKMOPTXY";
  constexpr std::u32string_view russian_list = U"асеорхукавсенкмортху";
  // Only in a word with a real Cyrillic letter: "Сoco" with a Latin "C"; "Cocо" of Latin letters stays Latin.
  bool look_alike = always;
  for (size_t pos = 0; pos < word.size();) {
    const auto code_point = DecodeUtf8(word, pos);
    look_alike = look_alike || (code_point.value >= U'А' && code_point.value <= U'я') || code_point.value == U'ё'
      || code_point.value == U'Ё';
    pos += code_point.size;
  }
  for (const char letter : word) {
    if (
      static_cast<unsigned char>(letter) < 0x80 && letter != '+' && latin_list.find(letter) == std::string_view::npos
    ) {
      look_alike = false;
    }
  }
  std::string russian;
  for (size_t pos = 0; pos < word.size();) {
    const auto code_point = DecodeUtf8(word, pos);
    pos += code_point.size;
    const auto value = code_point.value;
    if (value < 0x80 && value != U'+') {
      if (look_alike) {
        AppendUtf8(russian, russian_list[latin_list.find(static_cast<char>(value))]);
      }
      continue;
    }
    AppendUtf8(russian, ToLower(value));
  }
  return russian;
}

enum class TokenType { WORD, NUMBER, SYMBOL };

struct Token {
  TokenType type = TokenType::SYMBOL;
  std::string text;  // as written
  std::string lower; // a word in lower case
  bool space_before = false;
  bool line_start = false; // the first token of a line
};

std::vector<Token> Tokenize(std::string_view text);

inline bool StartsUpper(const Token& token) {
  const auto code_point = Decode(token.text, 0);
  return code_point && ToLower(code_point->value) != code_point->value;
}

inline std::string_view OutputSymbol(std::string_view symbol) {
  constexpr std::string_view quotes_list[] = {"«", "»", "“", "”", "„", "‟"};
  constexpr std::string_view dashes[] = {"—", "–", "‒", "―", "−"};
  if (std::find(std::begin(quotes_list), std::end(quotes_list), symbol) != std::end(quotes_list)) {
    return "\"";
  }
  if (std::find(std::begin(dashes), std::end(dashes), symbol) != std::end(dashes)) {
    return "-";
  }
  return symbol;
}

// Words joined by spaces.
class Words {
public:
  void Add(std::string_view words) {
    if (words.empty()) {
      return;
    }
    if (!text_.empty()) {
      text_ += ' ';
    }
    text_ += words;
  }

  const std::string& Text() const {
    return text_;
  }

private:
  std::string text_;
};

inline std::string ReadDigits(std::string_view digits) {
  Words words;
  for (const char digit : digits) {
    words.Add(Cardinal(static_cast<uint64_t>(digit - '0'), kNom, Gender::MASCULINE));
  }
  return words.Text();
}

inline uint64_t ValueOf(std::string_view digits) {
  uint64_t value = 0;
  for (const char digit : digits) {
    value = value * 10 + static_cast<uint64_t>(digit - '0');
  }
  return value;
}

// A group of a telephone number or a code: "45" -> "с+орок пять", "07" -> "ноль семь".
inline std::string ReadGroup(std::string_view digits) {
  if ((digits.size() > 1 && digits.front() == '0') || digits.size() > kMaxNumeralDigits) {
    return ReadDigits(digits);
  }
  return Cardinal(ValueOf(digits), kNom, Gender::MASCULINE);
}

// All readings of a noun of this gender and number.
inline std::vector<NounReading> AnyCase(Gender gender, std::initializer_list<bool> plurals) {
  std::vector<NounReading> readings;
  for (const bool plural : plurals) {
    for (int case_index = 0; case_index < kCaseCount; ++case_index) {
      readings.push_back({gender, plural, static_cast<Case>(case_index)});
    }
  }
  return readings;
}

inline std::string ToRoman(uint64_t value) {
  constexpr std::pair<std::string_view, uint64_t> roman_list[] = {
    {"M", 1000},
    {"CM", 900},
    {"D", 500},
    {"CD", 400},
    {"C", 100},
    {"XC", 90},
    {"L", 50},
    {"XL", 40},
    {"X", 10},
    {"IX", 9},
    {"V", 5},
    {"IV", 4},
    {"I", 1},
  };
  std::string roman;
  for (const auto& [symbol, amount] : roman_list) {
    for (; value >= amount; value -= amount) {
      roman += symbol;
    }
  }
  return roman;
}

// The value of a Roman numeral written the usual way ("XIV", not "XIIII" or "IXX"); 0 if it is not one.
inline uint64_t RomanValue(std::string_view text) {
  constexpr size_t max_length = 15;
  constexpr std::string_view digits_list = "IVXLCDM";
  constexpr uint64_t values[] = {1, 5, 10, 50, 100, 500, 1000};
  if (text.empty() || text.size() > max_length || text.find_first_not_of(digits_list) != std::string_view::npos) {
    return 0;
  }
  uint64_t value = 0;
  for (size_t pos = 0; pos < text.size(); ++pos) {
    const auto current = values[digits_list.find(text[pos])];
    const auto next = pos + 1 < text.size() ? values[digits_list.find(text[pos + 1])] : 0;
    value = current < next ? value - current : value + current;
  }
  return ToRoman(value) == text ? value : 0;
}

// The reading of a noun the governing word names first, else the direct cases first; masculine first.
inline std::optional<NounReading> PickReading(
  const std::vector<NounReading>& readings, const std::vector<GrammaticalCase>& governed
) {
  std::vector<GrammaticalCase> order = governed;
  // The dative before the prepositional: "посвящена XX в+еку".
  for (const auto grammatical_case :
       {kNom, Case::ACCUSATIVE, kGen, Case::DATIVE, Case::PREPOSITIONAL, Case::INSTRUMENTAL}) {
    order.push_back(grammatical_case);
  }
  for (const auto grammatical_case : order) {
    for (const auto gender : {Gender::MASCULINE, Gender::FEMININE, Gender::NEUTER}) {
      for (const auto& reading : readings) {
        if (reading.grammatical_case == grammatical_case && reading.gender == gender) {
          return reading;
        }
      }
    }
  }
  return std::nullopt;
}

// A number with what is written with it: "-5", "1 234,5", "2-го", "3-комнатную", "5 тыс. руб.", "$20".
struct Amount {
  size_t begin = 0;
  size_t end = 0; // tokens [begin, end)
  int sign = 0;
  std::string digits;
  std::string fraction;
  std::string suffix;
  std::string compound;
  std::vector<const Unit*> units;
  bool year_abbreviation = false; // "2001 г."
  bool year_plural = false;       // "гг."
};

// What follows a number.
struct NounContext {
  std::vector<NounReading> readings;
  OrdinalUse ordinal_use = OrdinalUse::NONE;
  const Noun* noun = nullptr;
  size_t position = 0;             // the token of the noun or the month, 0 if it is another number's
  int month = -1;                  // a month name in the genitive: "мая"
  bool month_abbreviation = false; // "янв."
  bool adjective = false;          // adjectives between the number and the noun: "1 свободное место"
  bool known = false;              // not a lexicon noun, but the dictionary of words knows it
  bool day_part = false;           // the next number of a chain is hours of a part of the day: "с 1 до 4 ночи"
  bool plural_only = false;        // a noun of the dictionary without a singular: "1 грабли"
};

class Normalization {
public:
  Normalization(
    const Lexicon& lexicon,
    const WordReader& words,
    const TextNormalizer::WordCheck& is_word,
    const WordInfo& word_info,
    std::string_view text
  )
    : lexicon_(lexicon)
    , words_(words)
    , is_word_(is_word)
    , word_info_(word_info)
    , tokens_(Tokenize(text)) {
    for (const auto& token : tokens_) {
      lower_cyrillic_ = lower_cyrillic_ || (token.type == TokenType::WORD && HasLowerCyrillic(token.text));
    }
  }

  std::vector<Piece> Run();

private:
  bool IsNumber(size_t index) const {
    return index < tokens_.size() && tokens_[index].type == TokenType::NUMBER;
  }

  bool IsWord(size_t index) const {
    return index < tokens_.size() && tokens_[index].type == TokenType::WORD;
  }

  bool IsSentenceEnd(size_t index) const {
    return IsSymbol(index, ".") || IsSymbol(index, "!") || IsSymbol(index, "?") || IsSymbol(index, "…");
  }

  bool IsSymbol(size_t index, std::string_view symbol) const {
    return index < tokens_.size() && tokens_[index].type == TokenType::SYMBOL && tokens_[index].text == symbol;
  }

  // No space before the token.
  bool Glued(size_t index) const {
    return index < tokens_.size() && !tokens_[index].space_before;
  }

  bool IsSign(size_t index) const {
    if (
      !(IsSymbol(index, "-") || IsSymbol(index, "−") || IsSymbol(index, "+")) || !IsNumber(index + 1)
      || !Glued(index + 1)
    ) {
      return false;
    }
    return index == 0 || tokens_[index].space_before || tokens_[index - 1].type == TokenType::SYMBOL;
  }

  const Noun* NounByLemma(std::string_view lemma) const {
    const auto* match = lexicon_.FindNoun(lemma);
    return match != nullptr && LookupKey(match->noun->singular[0]) == lemma ? match->noun : nullptr;
  }

  // A unit written from token `index`: the longest abbreviation, with its dot; "мм рт. ст." has spaces inside.
  std::optional<std::pair<const Unit*, size_t>> MatchUnit(size_t index) const;

  // A capitalized word at token `index`, after opening quotes or brackets too: "Доставка", "«Доставка".
  bool CapitalAt(size_t index) const {
    while (index < tokens_.size() && tokens_[index].type == TokenType::SYMBOL
           && (tokens_[index].text == "«" || tokens_[index].text == "\"" || tokens_[index].text == "„"
               || tokens_[index].text == "“" || tokens_[index].text == "(")) {
      ++index;
    }
    return IsWord(index) && StartsUpper(tokens_[index]);
  }

  // After the dot of an abbreviation, unless the dot ends the sentence.
  size_t SkipAbbreviationDot(size_t index) const {
    const auto ends_sentence = index + 1 >= tokens_.size() || CapitalAt(index + 1);
    return IsSymbol(index, ".") && Glued(index) && !ends_sentence ? index + 1 : index;
  }

  std::optional<Amount> ParseAmount(size_t index) const;

  // The cases the words before token `index` put a number in. A number of a chain ("через 1 ч 20 мин", "между 2 и 3")
  // takes the cases of the first one; each token is looked at once, so a long chain is linear.
  std::vector<GrammaticalCase> GovernedBefore(size_t index) const;

  // The cases the word right before token `index` gives, or the first token of the number before it in a chain.
  std::optional<size_t> GovernedStep(size_t index, std::vector<GrammaticalCase>& cases) const;

  // A number after "лампа": its name.
  bool IsLabel(size_t index) const {
    if (index == 0 || !tokens_[index].space_before) {
      return false;
    }
    // "№ 9264": a name, not a count.
    if (IsSymbol(index - 1, "№")) {
      return true;
    }
    // "т. 6", "с. 138", "кв. 3": a number after an abbreviated noun that stands before numbers names it.
    if (index >= 2 && IsSymbol(index - 1, ".") && Glued(index - 1) && IsWord(index - 2)) {
      if (const auto* abbreviations = lexicon_.FindAbbreviation(tokens_[index - 2].lower + ".")) {
        if (std::any_of(abbreviations->begin(), abbreviations->end(), [](const Abbreviation& abbreviation) {
              return abbreviation.noun != nullptr && abbreviation.condition == Abbreviation::Condition::NUMBER;
            })) {
          return true;
        }
      }
    }
    const auto* match = IsWord(index - 1) ? lexicon_.FindNoun(tokens_[index - 1].lower) : nullptr;
    return match != nullptr && match->noun->label;
  }

  // The first token of the number that ends at token `index`: "1 234,5".
  size_t NumberStart(size_t index) const {
    while (index > 0 && (!tokens_[index].space_before || IsNumber(index - 1))
           && tokens_[index - 1].type != TokenType::WORD) {
      --index;
    }
    return index;
  }

  // The noun after token `index`; `look_through` takes the noun of the next number too ("с 2 до 5 минут").
  NounContext NounAfter(size_t index, bool look_through, bool plural_context = false) const;

  // The noun of the next number as the noun of this one: "от 1 до 2 мин+ут" -> any case and number of "минута";
  // "с 2019 по 2024 год" takes an ordinal, "от 2 до 5 лет" does not.
  NounContext SharedNoun(const Amount& next) const;

  // A telephone number: "+7 (495) 123-45-67", "+7 999 123-45-67", "8 999 123-45-67", "+7 495 1234567": groups with
  // a pause between them, the code without brackets. Ten to twelve digits, or a '+' before; groups of three digits
  // after a short first one without a '+' are thousands ("2 500 000 000").
  std::optional<size_t> TryPhone(size_t index);

  // A long group of a telephone number in parts: "1234567" -> "123", "45", "67".
  static std::vector<std::string_view> PhoneParts(std::string_view group);

  // "The Weeknd", "AC/DC", "Guns N' Roses", "БИ-2", "10AGE", "5G": the longest name of the table of readings written
  // with several tokens from this one.
  std::optional<size_t> TryReading(size_t index);

  std::optional<size_t> TryNumber(size_t index);

  // "в 9.30": a time, not a decimal fraction: after a preposition of time, minutes of two digits, no noun after it.
  bool IsDotTime(size_t index) const;

  // "7:45", "2:15:30", "в 9.30".
  std::optional<size_t> TryTime(size_t index);

  // A time of day: "семь с+орок пять", "семь ноль ноль", "десять ноль два".
  static std::string ReadClock(
    uint64_t hours, std::initializer_list<std::optional<uint64_t>> parts, GrammaticalCase grammatical_case
  );

  // The case of a day without a preposition.
  std::vector<GrammaticalCase> DateCases(size_t index, size_t after) const;

  // The token after a date of a day and a month at `month`: its year with "года" or "г." if written.
  size_t DateEnd(size_t month) const;

  // The day of a date: "двадц+ать четвёртое", "п+ервого".
  std::string ReadDay(
    uint64_t day, size_t index, size_t after, const std::vector<GrammaticalCase>& governed, const std::string& suffix
  ) const;

  // A year after a month, with "года" or "г." after it if written: returns the token after them.
  size_t AddYear(Words& words, size_t index, bool always_noun) const;

  // "15.08", "05.06": a day and a month of two digits each, the month with a leading zero or the day with one.
  bool IsShortDate(size_t index) const;

  // "2026-11-24".
  std::optional<size_t> TryIsoDate(size_t index);

  // "12.04.1961".
  std::optional<size_t> TryNumericDate(size_t index);

  // "1.2.3", "192.168.0.1".
  std::optional<size_t> TryDotted(size_t index);

  // Telephone numbers and codes: "8-800-555-35-35", "8 916 000 00 01", "0451"; numbers beyond the numerals.
  std::optional<size_t> TrySequence(size_t index);

  // "5-7 дней", "10–15 минут", "20-30%".
  std::optional<size_t> TryRange(size_t index);

  // "XXI век", "в XIX веке", "XX–XXI вв.": ordinals agreed with the noun after; "Пётр I", "Глава IV", "при Петре I":
  // with the capitalized word before.
  std::optional<size_t> TryRoman(size_t index);

  // "5–7 мая": "с п+ятого по седьм+ое мая"; after a preposition both days in its case.
  std::optional<size_t> TryDayRange(size_t index, uint64_t from, const Amount& second);

  // "глава II": a noun that takes an ordinal.
  bool IsOrdinalNoun(size_t index) const {
    const auto* match = lexicon_.FindNoun(tokens_[index].lower);
    return match != nullptr && match->noun->ordinal_use != OrdinalUse::NONE;
  }

  // "2020–2022 годах", "2025/2026 гг.": two years with one plural noun are ordinals in the singular.
  std::optional<size_t> TryYearPair(size_t index, uint64_t from, const Amount& second);

  // "1/2" -> "одн+а втор+ая", "3/4" -> "три чётвёртых", "28/2" -> "дв+адцать в+осемь др+обь два", "2025/2026 гг.".
  std::optional<size_t> TrySlash(size_t index);

  bool IsDistance(const Amount& amount) const;
  size_t EmitAmount(const Amount& amount);

  // The cases a verb, a noun or an adjective right before the number gives: "достигает 100" - the genitive, "принял 21
  // пациента" - the accusative; not a short passive participle ("куплена 1 лампа").
  std::vector<GrammaticalCase> WordGovernment(size_t index) const;

  // "с" and a count without "до" or "по" after: "с 5000 человек" - "с пятью тысячами", but "с 5 до 7", "с 10 утра",
  // "с 1 января", "с 0,5 литра", "скидка с 5000 рублей".
  bool IsWithCount(const Amount& amount) const;

  size_t AddAmount(Words& words, const Amount& amount, const std::vector<GrammaticalCase>& governed, bool weak = false);

  // "тыс.", "млн": a unit after it is the counted one ("5 тыс. руб.").
  bool IsMultiplier(const Noun& noun) const {
    for (const auto* lemma : {"тысяча", "миллион", "миллиард", "триллион"}) {
      if (NounByLemma(lemma) == &noun) {
        return true;
      }
    }
    return false;
  }

  bool IsDayPart(size_t index) const {
    return IsWord(index) && lexicon_.IsServiceWord("day_part", tokens_[index].lower);
  }

  // "масса 1500 г.": grams, not a year.
  bool IsMassWord(size_t index) const {
    return PreviousWordIn(index, "mass");
  }

  // "версия 1.2", "версия 1.2.2024": numbers joined by dots.
  bool IsVersionWord(size_t index) const {
    return PreviousWordIn(index, "version");
  }

  bool PreviousWordIn(size_t index, std::string_view section) const {
    if (index > 1 && IsSymbol(index - 1, ".") && !tokens_[index - 1].space_before && tokens_[index].space_before) {
      --index; // "п. 3.2": the word of an abbreviation
      return IsWord(index - 1) && lexicon_.IsServiceWord(section, tokens_[index - 1].lower);
    }
    return index > 0 && tokens_[index].space_before && IsWord(index - 1)
      && lexicon_.IsServiceWord(section, tokens_[index - 1].lower);
  }

  // "остался 1 этаж": a word before a number that counts.
  bool IsCountingWord(size_t index) const {
    return PreviousWordIn(index, "counting");
  }

  // The number before a noun that needs its own forms: "одн+и сутки", "дв+ое суток".
  static std::string CountOfNoun(bool plural_only, uint64_t value, const NumeralReading& reading, std::string number);

  // An adjective written in the genitive singular after a fraction, read with the number: "белорусского" ->
  // "белорусский" after one, "белорусских" after more; others stay as written.
  static std::string AgreeAdjective(
    const std::string& word, uint64_t count, const NumeralReading& reading, Gender gender
  );

  size_t AddDecimal(Words& words, const Amount& amount, const std::vector<GrammaticalCase>& governed, uint64_t value);

  // A word of the text: Russian as written; capitals as an abbreviation if the text has small letters ("МЧС", not
  // "ВКЛЮЧИ СВЕТ"); Latin by the word reader; Latin letters inside a Russian word are the Russian letters they look
  // like ("cвeт"), or dropped ("играетqurt").
  size_t AddWord(size_t index);

  // "т. е.", "ул.", "д. 5", "Невский пр.": the longest abbreviation written from the token whose condition holds.
  std::optional<size_t> TryAbbreviation(size_t index);

  bool Holds(Abbreviation::Condition condition, size_t begin, size_t end) const;

  // "info@example.com", "www.example.ru", "https://example.org/docs?page=2", "index.html": the name and the host, the
  // path is left out.
  std::optional<size_t> TryNetAddress(size_t index);

  // A Russian word with Latin letters: letters that look like Russian ones are those ("cвeт"); otherwise the Latin
  // and the Russian parts are words of their own ("играетqurt" -> "играет курт").
  std::string ReadMixed(std::string_view word) const;

  // The readings of an unknown word by its ending, none for service words.
  std::vector<NounReading> Guess(std::string_view word) const {
    if (lexicon_.IsNotNoun(word)) {
      return {};
    }
    // "1 модель", "21 запись", "1 новость": a feminine noun in "-ь", the nominative and the accusative.
    const auto ends = [word](std::string_view ending) {
      return word.size() > ending.size() && word.compare(word.size() - ending.size(), ending.size(), ending) == 0;
    };
    if (ends("ость") || ends("есть") || ends("знь") || lexicon_.IsServiceWord("feminine", word)) {
      return {{Gender::FEMININE, false, kNom}, {Gender::FEMININE, false, Case::ACCUSATIVE}};
    }
    return GuessNounReadings(word);
  }

  // "1. Нарежьте", "2) Посолите": a number of one or two digits at the start of a line with "." or ")" and a capital.
  bool IsListItem(size_t index) const {
    constexpr size_t max_item_digits = 2;
    return IsNumber(index) && tokens_[index].line_start && tokens_[index].text.size() <= max_item_digits
      && (IsSymbol(index + 1, ".") || IsSymbol(index + 1, ")")) && Glued(index + 1) && IsWord(index + 2)
      && tokens_[index + 2].space_before && StartsUpper(tokens_[index + 2]);
  }

  // "нас было 3": a word of persons right before the number or before the verb before it.
  bool CountsPersons(size_t index) const {
    for (size_t back = 1; back <= 2 && back <= index; ++back) {
      if (!IsWord(index - back)) {
        return false;
      }
      if (lexicon_.IsServiceWord("persons", tokens_[index - back].lower)) {
        return true;
      }
    }
    return false;
  }

  // The cases a word of the government tables before token `index` gives, not a guess by the ending: "датирован".
  std::vector<GrammaticalCase> TableGovernment(size_t index) const {
    if (index == 0 || !tokens_[index].space_before || !IsWord(index - 1)) {
      return {};
    }
    return lexicon_.WordGovernedCases(tokens_[index - 1].lower);
  }

  // The preposition before a number read in words agrees with its first word: "со ст+а", "во втор+ом", "об
  // од+иннадцати", "ко втор+ому".
  void AgreePreposition(size_t first_piece);

  bool IsLatinWord(size_t index) const {
    const auto first = IsWord(index) ? Decode(tokens_[index].text, 0) : std::nullopt;
    return first && IsLatinLetter(first->value);
  }

  // "?!", "!!!", "...": one boundary, the question first; closing quotes right after it go with the sentence. An
  // ellipsis before a small letter is a pause, not a boundary: "Ну… давай" -> "ну. давай".
  size_t AddSentenceEnd(size_t index);

  // "А. С. Пушкин", "Пушкин А. С.": a capital letter with a dot next to another one or to a capitalized word is an
  // initial, its dot is not a boundary.
  std::optional<size_t> TryInitial(size_t index);

  // The token after the first `close` from `index` within a few tokens, or `index` if there is none.
  size_t SkipTo(size_t index, std::string_view close, bool russian_allowed) const;

  // Marks of the text stay; '+', '&', '№' and signs between numbers become words; tags, entities and brackets of
  // Latin are dropped with what is inside them; other symbols are dropped.
  size_t AddSymbol(size_t index);

  void Add(std::string_view text, PieceKind kind, bool space_before);

  const Lexicon& lexicon_;
  const WordReader& words_;
  const TextNormalizer::WordCheck& is_word_;
  const WordInfo& word_info_;
  // GovernedBefore by the token.
  mutable std::unordered_map<size_t, std::vector<GrammaticalCase>> governed_;
  // WordGovernment of the numbers of a chain by the token.
  mutable std::unordered_map<size_t, std::vector<GrammaticalCase>> word_governed_;
  bool lower_cyrillic_ = false;
  std::vector<Token> tokens_;
  std::vector<Piece> pieces_;
  // The gender of the last noun counted in the sentence.
  std::optional<Gender> counted_gender_;
};

} // namespace giga_edge::vits::normalization
