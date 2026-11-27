#include "giga_edge/modules/tts/interpreter/backends/vits/text/latin.h"

#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/lexicon.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/utf8.h"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace giga_edge::vits {

namespace {

constexpr std::string_view kReadingsFile = "readings.tsv";

// By the letter, a to z; the names the stress stage knows as letters (hyphen.tsv).
constexpr std::array<std::string_view, 26> kLatinLetters = {
  "эй", "би", "си", "ди",  "и",  "эф", "джи", "эйч", "ай", "джей",   "кей", "эл",  "эм",
  "эн", "оу", "пи", "кью", "ар", "эс", "ти",  "ю",   "ви", "дабл-ю", "икс", "уай", "зэд",
};

// By the letter, а to я.
constexpr std::array<std::string_view, 32> kCyrillicLetters = {
  "а",  "бэ", "вэ", "гэ", "дэ", "е",  "жэ", "зэ", "и",  "й",  "ка", "эль", "эм", "эн", "о", "пэ",
  "эр", "эс", "тэ", "у",  "эф", "ха", "цэ", "чэ", "ша", "ща", "",   "ы",   "",   "э",  "ю", "я",
};

// Rules of transliteration, the longest spelling first at every place.
struct Rule {
  std::string_view latin;
  std::string_view russian;
};

constexpr Rule kRules[] = {
  {"tch", "ч"}, {"sch", "ш"}, {"igh", "ай"}, {"ough", "оу"}, {"sh", "ш"},  {"ch", "ч"},  {"zh", "ж"},  {"kh", "х"},
  {"ph", "ф"},  {"th", "т"},  {"ck", "к"},   {"qu", "кв"},   {"wh", "у"},  {"gh", "г"},  {"ee", "и"},  {"oo", "у"},
  {"ea", "и"},  {"ou", "ау"}, {"ai", "эй"},  {"ay", "эй"},   {"ey", "эй"}, {"ei", "ай"}, {"oy", "ой"}, {"oi", "ой"},
  {"au", "о"},  {"aw", "о"},  {"ie", "и"},   {"x", "кс"},    {"j", "дж"},  {"w", "у"},   {"q", "к"},   {"h", "х"},
  {"a", "а"},   {"b", "б"},   {"c", "к"},    {"d", "д"},     {"e", "е"},   {"f", "ф"},   {"g", "г"},   {"i", "и"},
  {"k", "к"},   {"l", "л"},   {"m", "м"},    {"n", "н"},     {"o", "о"},   {"p", "п"},   {"r", "р"},   {"s", "с"},
  {"t", "т"},   {"u", "у"},   {"v", "в"},    {"z", "з"},
};

bool IsLatinVowel(char letter) {
  return std::string_view("aeiouy").find(letter) != std::string_view::npos;
}

char LowerAscii(char letter) {
  return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter - 'A' + 'a') : letter;
}

std::string LowerLatin(std::string_view word) {
  std::string lower(word);
  for (auto& letter : lower) {
    letter = LowerAscii(letter);
  }
  return lower;
}

// The plain letters of a Latin letter with a diacritic, lower-case: "å" -> "a", "ß" -> "ss", "Ł" -> "l"; empty for
// another code point.
std::string_view PlainLatin(char32_t letter) {
  // U+00C0..U+00FF, then U+0100..U+017F.
  constexpr std::array<std::string_view, 64> latin1 = {
    "a", "a", "a", "a", "a", "a", "ae", "c", "e",  "e",  "e", "e", "i", "i", "i", "i", "d",  "n", "o",  "o", "o", "o",
    "o", "",  "o", "u", "u", "u", "u",  "y", "th", "ss", "a", "a", "a", "a", "a", "a", "ae", "c", "e",  "e", "e", "e",
    "i", "i", "i", "i", "d", "n", "o",  "o", "o",  "o",  "o", "",  "o", "u", "u", "u", "u",  "y", "th", "y",
  };
  struct Run {
    std::string_view plain;
    int count;
  };
  constexpr Run extended_a[] = {
    {"a", 6}, {"c", 8}, {"d", 4},  {"e", 10}, {"g", 8}, {"h", 4}, {"i", 10}, {"ij", 2}, {"j", 2}, {"k", 3}, {"l", 10},
    {"n", 9}, {"o", 6}, {"oe", 2}, {"r", 6},  {"s", 8}, {"t", 6}, {"u", 12}, {"w", 2},  {"y", 3}, {"z", 6}, {"s", 1},
  };
  constexpr char32_t latin1_first = 0xC0;
  constexpr char32_t extended_first = 0x100;
  constexpr char32_t extended_end = 0x180;
  if (letter >= latin1_first && letter < extended_first) {
    return latin1[letter - latin1_first];
  }
  if (letter >= extended_first && letter < extended_end) {
    auto index = static_cast<int>(letter - extended_first);
    for (const auto& run : extended_a) {
      if (index < run.count) {
        return run.plain;
      }
      index -= run.count;
    }
  }
  return {};
}

// A capital of Latin-1 Supplement or Latin Extended-A: "Å", "Č", "Ł".
bool IsUpperLatin(char32_t letter) {
  constexpr char32_t latin1_first = 0xC0;
  constexpr char32_t latin1_lower = 0xDF; // "ß" and the small letters after it
  constexpr char32_t extended_first = 0x100;
  constexpr char32_t odd_upper_first = 0x139; // "Ĺ".."ň" and "Ź".."ž": the capital is odd
  constexpr char32_t odd_upper_last = 0x148;
  constexpr char32_t y_diaeresis = 0x178;
  constexpr char32_t z_first = 0x179;
  constexpr char32_t extended_end = 0x180;
  if (letter >= latin1_first && letter < latin1_lower) {
    return letter != 0xD7;
  }
  if (letter < extended_first || letter >= extended_end || letter == 0x138 || letter == 0x149 || letter == 0x17F) {
    return false;
  }
  if (letter == y_diaeresis) {
    return true;
  }
  const auto odd = (letter >= odd_upper_first && letter <= odd_upper_last) || letter >= z_first;
  return (letter % 2 == 1) == odd;
}

// Latin letters without their diacritics and combining marks: "Måneskin" -> "Maneskin", "Beyoncé" -> "Beyonce".
std::string PlainLatinWord(std::string_view word) {
  std::string plain;
  for (size_t pos = 0; pos < word.size();) {
    const auto code_point = DecodeUtf8(word, pos);
    constexpr char32_t combining_first = 0x300;
    constexpr char32_t combining_last = 0x36F;
    const auto letters = PlainLatin(code_point.value);
    if (!letters.empty()) {
      for (const char letter : letters) {
        plain += IsUpperLatin(code_point.value) ? static_cast<char>(letter - 'a' + 'A') : letter;
      }
    } else if (code_point.value < combining_first || code_point.value > combining_last) {
      plain.append(word, pos, code_point.size);
    }
    pos += code_point.size;
  }
  return plain;
}

// Cyrillic capitals to lower case and the other way round in the table key; Latin letters without diacritics.
std::string LowerKey(std::string_view word) {
  std::string key;
  const auto plain = PlainLatinWord(word);
  word = plain;
  for (size_t pos = 0; pos < word.size();) {
    const auto code_point = DecodeUtf8(word, pos);
    auto value = code_point.value;
    if (value >= U'A' && value <= U'Z') {
      key += static_cast<char>(value - U'A' + U'a');
    } else if ((value >= U'А' && value <= U'Я') || value == U'Ё') {
      value = value == U'Ё' ? U'ё' : value + (U'а' - U'А');
      key += static_cast<char>(0xC0 | (value >> 6));
      key += static_cast<char>(0x80 | (value & 0x3F));
    } else {
      key.append(word, pos, code_point.size);
    }
    pos += code_point.size;
  }
  return key;
}

void AppendLetter(std::string& spelled, std::string_view name) {
  if (name.empty()) {
    return;
  }
  if (!spelled.empty()) {
    spelled += '-';
  }
  spelled += name;
}

} // namespace

std::string SpellLatin(std::string_view word) {
  std::string spelled;
  for (const char letter : word) {
    const auto lower = LowerAscii(letter);
    if (lower >= 'a' && lower <= 'z') {
      AppendLetter(spelled, kLatinLetters[static_cast<size_t>(lower - 'a')]);
    }
  }
  return spelled;
}

std::string SpellCyrillic(std::string_view word) {
  std::string spelled;
  for (size_t pos = 0; pos < word.size();) {
    const auto code_point = DecodeUtf8(word, pos);
    auto value = code_point.value;
    if (value >= U'А' && value <= U'Я') {
      value += U'а' - U'А';
    }
    if (value == U'ё' || value == U'Ё') {
      AppendLetter(spelled, "ё");
    } else if (value >= U'а' && value <= U'я') {
      AppendLetter(spelled, kCyrillicLetters[value - U'а']);
    }
    pos += code_point.size;
  }
  return spelled;
}

std::string Transliterate(std::string_view word) {
  const auto lower = LowerLatin(word);
  std::string russian;
  for (size_t pos = 0; pos < lower.size();) {
    const auto letter = lower[pos];
    // "y" is a consonant before a vowel and at the start, a vowel elsewhere.
    if (letter == 'y') {
      const auto consonant = pos == 0 || (pos + 1 < lower.size() && IsLatinVowel(lower[pos + 1]));
      russian += consonant ? "й" : "и";
      ++pos;
      continue;
    }
    // "c" before e, i, y sounds as "s".
    if (
      letter == 'c' && pos + 1 < lower.size() && std::string_view("eiy").find(lower[pos + 1]) != std::string_view::npos
    ) {
      russian += "с";
      ++pos;
      continue;
    }
    // A final "e" after a consonant is silent in a word of more than three letters: "imagine".
    if (letter == 'e' && pos + 1 == lower.size() && lower.size() > 3 && !IsLatinVowel(lower[pos - 1])) {
      ++pos;
      continue;
    }
    // "e" at the start sounds as "э"; "er" at the end as "эр".
    if (letter == 'e' && (pos == 0 || (pos + 2 == lower.size() && lower[pos + 1] == 'r'))) {
      russian += "э";
      ++pos;
      continue;
    }
    bool matched = false;
    for (const auto& rule : kRules) {
      if (lower.compare(pos, rule.latin.size(), rule.latin) == 0) {
        russian += rule.russian;
        pos += rule.latin.size();
        matched = true;
        break;
      }
    }
    if (!matched) {
      ++pos;
    }
  }
  return russian;
}

WordReader::WordReader(IDataReader& reader, const std::string& directory, LatinWordReading read_word)
  : custom_reader_(read_word != nullptr)
  , read_word_(read_word ? std::move(read_word) : LatinWordReading(Transliterate)) {
  const auto prefix = directory.empty() || directory.back() == '/' ? directory : directory + "/";
  const auto path = prefix + std::string(kReadingsFile);
  const auto text = data_reader::ToString(data_reader::EnsureNotNull(reader.ReadTransientData(path), path));
  int number = 0;
  size_t begin = 0;
  while (begin < text.size()) {
    auto end = text.find('\n', begin);
    if (end == std::string::npos) {
      end = text.size();
    }
    std::string_view line(text.data() + begin, end - begin);
    begin = end + 1;
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
    const auto tab = line.find('\t');
    if (
      tab == std::string_view::npos || tab == 0 || tab + 1 == line.size()
      || line.find('\t', tab + 1) != std::string_view::npos
    ) {
      throw std::runtime_error(
        path + ":" + std::to_string(number) + ": expected a word and its reading separated by a tab"
      );
    }
    // The written word may hold '+' ("black+decker"), the reading only before a vowel.
    try {
      CheckSpoken(line.substr(0, tab), false);
      CheckSpoken(line.substr(tab + 1));
      CheckReading(line.substr(tab + 1));
    } catch (const std::exception& error) {
      throw std::runtime_error(path + ":" + std::to_string(number) + ": " + error.what());
    }
    const auto key = LowerKey(line.substr(0, tab));
    for (size_t size = 1; size <= key.size(); ++size) {
      prefixes_.insert(key.substr(0, size));
    }
    if (!readings_.emplace(key, std::string(line.substr(tab + 1))).second) {
      throw std::runtime_error(path + ":" + std::to_string(number) + ": the word is given twice");
    }
  }
}

bool WordReader::IsTablePrefix(const std::string& lower) const {
  // Folded as a key is, when a Latin letter with a diacritic or a combining mark is there: "michael bublé".
  const auto folded = std::any_of(lower.begin(), lower.end(), [](char byte) {
    constexpr unsigned char first_lead = 0xC3;
    constexpr unsigned char last_lead = 0xCD;
    return static_cast<unsigned char>(byte) >= first_lead && static_cast<unsigned char>(byte) <= last_lead;
  });
  return prefixes_.count(folded ? LowerKey(lower) : lower) != 0;
}

std::string WordReader::FromTable(std::string_view word) const {
  const auto found = readings_.find(LowerKey(word));
  return found == readings_.end() ? std::string() : found->second;
}

std::string WordReader::ReadLatin(std::string_view written) const {
  if (auto reading = FromTable(written); !reading.empty()) {
    return reading;
  }
  // The rules read the plain letters; a reader given from outside gets the word as written, with its diacritics.
  const auto plain = PlainLatinWord(written);
  const std::string_view word = plain;
  size_t capitals = 0;
  size_t vowels = 0;
  for (const char letter : word) {
    capitals += letter >= 'A' && letter <= 'Z' ? 1 : 0;
    vowels += IsLatinVowel(LowerAscii(letter)) ? 1 : 0;
  }
  // Capitals: "USB", "GPT", "HDMI" by letters; "NASA", "PHARAOH" as words. A single letter by its name.
  constexpr size_t spelled_length = 3;
  constexpr size_t long_abbreviation = 4;
  const auto all_capitals = capitals == word.size();
  if (
    word.size() == 1 || vowels == 0
    || (all_capitals && (word.size() <= spelled_length || (word.size() == long_abbreviation && vowels <= 1)))
  ) {
    return SpellLatin(word);
  }
  return custom_reader_ ? read_word_(written) : read_word_(word);
}

std::string WordReader::ReadAbbreviation(std::string_view word) const {
  if (auto reading = FromTable(word); !reading.empty()) {
    return reading;
  }
  // "ООН", "ГОСТ" are read as words; "МГУ", "США", "ЖКХ", "ТВ" letter by letter: no vowel, two consonants at the
  // start, or two letters.
  const std::u32string_view vowels_list = U"АЕЁИОУЫЭЮЯ";
  std::u32string letters;
  for (size_t pos = 0; pos < word.size();) {
    const auto code_point = DecodeUtf8(word, pos);
    letters += code_point.value;
    pos += code_point.size;
  }
  const auto is_vowel = [&vowels_list](char32_t letter) {
    return vowels_list.find(letter) != std::u32string_view::npos;
  };
  bool has_vowel = false;
  for (const auto letter : letters) {
    has_vowel = has_vowel || is_vowel(letter);
  }
  const auto two_consonants = letters.size() >= 2 && !is_vowel(letters[0]) && !is_vowel(letters[1]);
  if (!has_vowel || two_consonants || letters.size() == 2) {
    return SpellCyrillic(word);
  }
  return LowerKey(word);
}

} // namespace giga_edge::vits
