#include "giga_edge/modules/tts/interpreter/backends/vits/text/foreign.h"

#include <array>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace giga_edge::vits {

namespace {

enum class Origin { ENGLISH, GERMAN, CHINESE, SLAVIC, OPEN };

struct Spelling {
  std::string_view latin;
  std::string_view russian;
};

// Letters of one origin, longer ones first; they come before the common ones.
constexpr std::array<Spelling, 12> kGerman = {{
  {"tsch", "ч"},
  {"sch", "ш"},
  {"ch", "х"},
  {"tz", "ц"},
  {"ei", "ай"},
  {"ai", "ай"},
  {"ie", "и"},
  {"eu", "ой"},
  {"au", "ау"},
  {"z", "ц"},
  {"v", "ф"},
  {"j", "й"},
}};
constexpr std::array<Spelling, 14> kChinese = {{
  {"ng", "н"},
  {"ai", "ай"},
  {"ei", "эй"},
  {"iao", "яо"},
  {"ia", "я"},
  {"ie", "е"},
  {"iu", "ю"},
  {"zh", "чж"},
  {"x", "с"},
  {"q", "ц"},
  {"c", "ц"},
  {"z", "цз"},
  {"j", "цз"},
  {"e", "э"},
}};
constexpr std::array<Spelling, 10> kSlavic = {{
  {"tsky", "цкий"},
  {"tch", "ч"},
  {"ai", "ай"},
  {"skaya", "ская"},
  {"skiy", "ский"},
  {"sky", "ский"},
  {"iy", "ий"},
  {"yy", "ый"},
  {"ei", "ей"},
  {"j", "й"},
}};
constexpr std::array<Spelling, 3> kOpen = {{{"ai", "ай"}, {"ei", "ей"}, {"j", "дж"}}};
constexpr std::array<Spelling, 35> kCommon = {{
  {"shch", "щ"}, {"sh", "ш"}, {"ch", "ч"}, {"zh", "ж"},  {"kh", "х"}, {"ts", "ц"}, {"ph", "ф"},
  {"th", "т"},   {"ck", "к"}, {"gh", "г"}, {"qu", "кв"}, {"a", "а"},  {"b", "б"},  {"c", "к"},
  {"d", "д"},    {"f", "ф"},  {"g", "г"},  {"h", "х"},   {"i", "и"},  {"j", "дж"}, {"k", "к"},
  {"l", "л"},    {"m", "м"},  {"n", "н"},  {"o", "о"},   {"p", "п"},  {"q", "к"},  {"r", "р"},
  {"s", "с"},    {"t", "т"},  {"u", "у"},  {"v", "в"},   {"w", "в"},  {"x", "кс"}, {"z", "з"},
}};

// a, e, i, o, u: 'y' is a consonant here.
bool IsPlainVowel(char letter) {
  return letter == 'a' || letter == 'e' || letter == 'i' || letter == 'o' || letter == 'u';
}

bool Has(std::string_view word, std::string_view part) {
  return word.find(part) != std::string_view::npos;
}

bool EndsWith(std::string_view word, std::string_view ending) {
  return word.size() >= ending.size() && word.substr(word.size() - ending.size()) == ending;
}

bool StartsWith(std::string_view word, std::string_view start) {
  return word.substr(0, start.size()) == start;
}

// Consonant-vowel pairs: every run of consonants is one letter, a double letter or a known pair, after an optional
// m, n, l, r or s that closes the previous syllable; vowel runs are at most two letters.
bool HasOpenSyllables(std::string_view word) {
  if (word.size() < 4 || !IsPlainVowel(word.back()) || word.back() == 'e' || Has(word, "w")) {
    return false;
  }
  for (const std::string_view english :
       {"th", "ph", "ck", "ee", "oo", "ea", "ou", "oa", "ow", "ey", "ay", "oy", "qu", "ie"}) {
    if (Has(word, english)) {
      return false;
    }
  }
  for (size_t begin = 0; begin < word.size();) {
    const bool vowels = IsPlainVowel(word[begin]);
    size_t end = begin;
    while (end < word.size() && IsPlainVowel(word[end]) == vowels) {
      ++end;
    }
    auto run = word.substr(begin, end - begin);
    begin = end;
    if (vowels) {
      if (run.size() > 2) {
        return false;
      }
      continue;
    }
    if (run.size() >= 2 && std::string_view("mnlrs").find(run[0]) != std::string_view::npos) {
      run.remove_prefix(1);
    }
    const bool known_pair = run.size() == 2
      && (run[0] == run[1] || run == "ch" || run == "sh" || run == "gh" || run == "ts" || run[1] == 'y');
    if (run.size() > 1 && !known_pair) {
      return false;
    }
  }
  return true;
}

constexpr std::array<std::string_view, 23> kInitials = {
  "zh", "ch", "sh", "b", "p", "m", "f", "d", "t", "n", "l", "g", "k", "h", "j", "q", "x", "r", "z", "c", "s", "y", "w",
};
constexpr std::array<std::string_view, 35> kFinals = {
  "iang", "iong", "uang", "ueng", "ang", "eng", "ong", "iao", "ian", "ing", "uai", "uan",
  "ai",   "ei",   "ao",   "ou",   "an",  "en",  "er",  "ia",  "ie",  "iu",  "in",  "ua",
  "uo",   "ui",   "un",   "ue",   "ve",  "a",   "o",   "e",   "i",   "u",   "v",
};

// Whether the word is cut into syllables of pinyin whole: an initial (or none) and a final; "wi" is not one.
bool IsPinyin(std::string_view word) {
  std::array<bool, kMaxLatinLetters + 1> reached{};
  if (word.size() > kMaxLatinLetters) {
    return false;
  }
  reached[0] = true;
  for (size_t pos = 0; pos < word.size(); ++pos) {
    if (!reached[pos]) {
      continue;
    }
    for (size_t initial = 0; initial <= kInitials.size(); ++initial) {
      const auto start = initial == kInitials.size() ? std::string_view() : kInitials[initial];
      if (!StartsWith(word.substr(pos), start)) {
        continue;
      }
      for (const auto final : kFinals) {
        if (StartsWith(word.substr(pos + start.size()), final) && !(start == "w" && final[0] == 'i')) {
          reached[pos + start.size() + final.size()] = true;
        }
      }
    }
  }
  return reached[word.size()];
}

// "sch" of German, but not of English prefixes and words: "dischargeable", "preschool", "interscholastic".
bool HasGermanSch(std::string_view word) {
  for (size_t pos = word.find("sch"); pos != std::string_view::npos; pos = word.find("sch", pos + 1)) {
    const auto before = word.substr(0, pos);
    const auto rest = word.substr(pos);
    const bool english = StartsWith(rest, "school") || StartsWith(rest, "schedul") || StartsWith(rest, "schem")
      || StartsWith(rest, "schism") || StartsWith(word, "eschew") || StartsWith(rest, "scholar")
      || StartsWith(rest, "scholastic") || (EndsWith(before, "di") && StartsWith(rest, "scharge"))
      || (EndsWith(before, "mi")
          && (StartsWith(rest, "schief") || StartsWith(rest, "schiev") || StartsWith(rest, "schance")));
    if (!english) {
      return true;
    }
  }
  return false;
}

// An ending of Russian surnames: "-ov", "-eva", "-sky" after a consonant ("trotsky", not "husky").
// "-ov", "-ev"; "-ova", "-eva" after a consonant, a vowel and 'n' excepted ("smirnova", not "casanova", "geneva").
bool HasSlavicEnding(std::string_view word) {
  if (word == "improv" || word == "improvs") {
    return false;
  }
  for (const std::string_view ending : {"ov", "ev"}) {
    if (EndsWith(word, ending) && word.size() > ending.size() + 1) {
      return true;
    }
  }
  for (const std::string_view ending : {"ova", "eva"}) {
    const size_t before = word.size() - ending.size() - 1;
    if (
      EndsWith(word, ending) && word.size() > ending.size() + 1 && !IsPlainVowel(word[before])
      && !(word[before] == 'n' && before > 0 && IsPlainVowel(word[before - 1]))
    ) {
      return true;
    }
  }
  for (const std::string_view ending : {"sky", "skiy", "skaya"}) {
    if (EndsWith(word, ending) && word.size() > ending.size() && !IsPlainVowel(word[word.size() - ending.size() - 1])) {
      return true;
    }
  }
  return false;
}

// "kh" at the start or between vowels: "khalil", "rakhim", "kazakh", but not "bark-hurst", "steak-house",
// "brook-haven".
bool HasSlavicKh(std::string_view word) {
  for (size_t pos = word.find("kh"); pos != std::string_view::npos; pos = word.find("kh", pos + 1)) {
    const bool after_two_vowels = pos >= 2 && IsPlainVowel(word[pos - 1]) && IsPlainVowel(word[pos - 2]);
    if (
      (pos == 0 || IsPlainVowel(word[pos - 1])) && (pos + 2 == word.size() || IsPlainVowel(word[pos + 2]))
      && !after_two_vowels
    ) {
      return true;
    }
  }
  return false;
}

bool HasChineseSigns(std::string_view word) {
  if (word == "zhanna" || word == "zhenya" || HasSlavicEnding(word)) {
    return false;
  }
  return (StartsWith(word, "xi") || StartsWith(word, "xu") || StartsWith(word, "qi") || StartsWith(word, "zh")
          || Has(word, "iao") || Has(word, "iang") || Has(word, "uang") || word == "shenzhen" || word == "beijing"
          || word == "shanghai")
    && IsPinyin(word);
}

// Shared with the Chinese route: ambiguous Slavic names must not fall between the two classifiers.
bool HasSlavicZh(std::string_view word) {
  for (size_t pos = word.find("zh"); pos != std::string_view::npos; pos = word.find("zh", pos + 1)) {
    const auto after = word.substr(pos + 2, 2);
    if ((pos == 0 || (word[pos - 1] != 'z' && word[pos - 1] != 't')) && after != "ei" && after != "au") {
      return !HasChineseSigns(word);
    }
  }
  return false;
}

bool HasStrongSlavicSigns(std::string_view word) {
  return HasSlavicZh(word) || Has(word, "shch") || HasSlavicKh(word) || HasSlavicEnding(word);
}

Origin GuessOrigin(std::string_view word) {
  if (Has(word, "'") || word.size() > kMaxLatinLetters) {
    return Origin::ENGLISH;
  }
  if (HasChineseSigns(word)) {
    return Origin::CHINESE;
  }
  if (
    HasGermanSch(word) || EndsWith(word, "tz") || EndsWith(word, "stein") || EndsWith(word, "mann")
    || EndsWith(word, "berg") || EndsWith(word, "burg") || EndsWith(word, "heim")
  ) {
    return Origin::GERMAN;
  }
  if (HasStrongSlavicSigns(word) || Has(word, "yu")) {
    return Origin::SLAVIC;
  }
  for (const std::string_view ending : {"iy", "yy", "ei"}) {
    if (EndsWith(word, ending) && word.size() > ending.size() + 1) {
      return Origin::SLAVIC;
    }
  }
  return HasOpenSyllables(word) ? Origin::OPEN : Origin::ENGLISH;
}

template<size_t Size>
const Spelling* Match(const std::array<Spelling, Size>& table, std::string_view rest) {
  for (const auto& spelling : table) {
    if (StartsWith(rest, spelling.latin)) {
      return &spelling;
    }
  }
  return nullptr;
}

const Spelling* MatchOrigin(Origin origin, std::string_view rest) {
  switch (origin) {
    case Origin::GERMAN:
      return Match(kGerman, rest);
    case Origin::CHINESE:
      return Match(kChinese, rest);
    case Origin::SLAVIC:
      return Match(kSlavic, rest);
    case Origin::OPEN:
      return Match(kOpen, rest);
    default:
      return nullptr;
  }
}

std::string_view IotatedVowel(char vowel) {
  switch (vowel) {
    case 'a':
      return "я";
    case 'u':
      return "ю";
    case 'o':
      return "ё";
    case 'e':
      return "е";
    default:
      return "и";
  }
}

constexpr std::array<std::string_view, 10> kVowels = {"а", "е", "ё", "и", "о", "у", "ы", "э", "ю", "я"};

bool IsRussianVowelAt(std::string_view text, size_t pos) {
  for (const auto vowel : kVowels) {
    if (text.substr(pos, vowel.size()) == vowel) {
      return true;
    }
  }
  return false;
}

// The stressed vowel among `vowels` (byte offsets in `text`); `diphthongs` are those of German ei, eu, au.
size_t StressedVowel(
  Origin origin, const std::string& text, const std::vector<size_t>& vowels, const std::vector<size_t>& diphthongs
) {
  for (const auto vowel : vowels) {
    if (text.compare(vowel, 2, "ё") == 0) {
      return vowel;
    }
  }
  const size_t last = vowels.back();
  const size_t penultimate = vowels.size() > 1 ? vowels[vowels.size() - 2] : last;
  switch (origin) {
    case Origin::GERMAN:
      return diphthongs.empty() ? vowels.front() : diphthongs.back();
    case Origin::CHINESE: {
      // The first vowel of the last syllable: "сяом+и", "гуанчж+оу".
      size_t index = vowels.size() - 1;
      if (index > 0 && vowels[index - 1] + 2 == vowels[index]) {
        --index;
      }
      return vowels[index];
    }
    default:
      return penultimate;
  }
}

} // namespace

std::string ReadForeign(std::string_view word) {
  const Origin origin = GuessOrigin(word);
  if (origin == Origin::ENGLISH) {
    return {};
  }
  std::string text;
  std::vector<size_t> vowels;
  std::vector<size_t> diphthongs;
  // Russian letters are two bytes each.
  const auto append = [&](std::string_view letters, bool diphthong = false) {
    for (size_t pos = 0; pos < letters.size(); pos += 2) {
      if (IsRussianVowelAt(letters, pos)) {
        if (diphthong && pos == 0) {
          diphthongs.push_back(text.size() + pos);
        }
        vowels.push_back(text.size() + pos);
      }
    }
    text += letters;
  };
  for (size_t i = 0; i < word.size();) {
    const char letter = word[i];
    const bool after_vowel = i > 0 && IsPlainVowel(word[i - 1]);
    const bool after_consonant = i > 0 && !after_vowel;
    const auto rest = word.substr(i);
    if (origin == Origin::GERMAN && (i == 0 || after_consonant) && (StartsWith(rest, "st") || StartsWith(rest, "sp"))) {
      append(rest[1] == 't' ? "шт" : "шп");
      i += 2;
      continue;
    }
    if (
      origin == Origin::GERMAN && (i == 0 || after_vowel) && letter == 's' && rest.size() > 1 && IsPlainVowel(rest[1])
    ) {
      append("з"); // "sonne" -> "з+онне", "gesaffelstein" -> "гезаффелшт+айн"
      ++i;
      continue;
    }
    if (const auto* own = MatchOrigin(origin, rest); own != nullptr) {
      // ei, ai, eu, au: two Russian letters, a vowel first
      const bool diphthong = origin == Origin::GERMAN && own->russian.size() == 4 && IsRussianVowelAt(own->russian, 0);
      append(own->russian, diphthong);
      i += own->latin.size();
      continue;
    }
    if (letter == 'y') {
      if (i + 1 < word.size() && IsPlainVowel(word[i + 1])) {
        if (origin == Origin::SLAVIC && after_consonant && word != "zhenya") {
          append("ь"); // "ilya" -> "илья"
        }
        append(IotatedVowel(word[i + 1]));
        i += 2;
      } else {
        append(after_vowel ? "й" : (origin == Origin::SLAVIC ? "ы" : "и"));
        ++i;
      }
      continue;
    }
    if (letter == 'e') {
      // A Russian 'е' is soft and iotated at the start: 'э' there and after a vowel, but Slavic names keep 'е'.
      append((i == 0 || after_vowel) && origin != Origin::SLAVIC ? "э" : "е");
      ++i;
      continue;
    }
    if (const auto* spelling = Match(kCommon, rest); spelling != nullptr) {
      append(spelling->russian);
      i += spelling->latin.size();
    } else {
      ++i; // not a Latin letter: the caller passes none
    }
  }
  if (vowels.empty()) {
    return {};
  }
  if (origin != Origin::SLAVIC) {
    text.insert(StressedVowel(origin, text, vowels, diphthongs), "+");
  }
  return text;
}

bool IsSurelyForeign(std::string_view word) {
  return HasStrongSlavicSigns(word);
}

} // namespace giga_edge::vits
