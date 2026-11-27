#include "giga_edge/modules/tts/interpreter/backends/vits/text/english.h"

#include "giga_edge/modules/tts/interpreter/backends/vits/encoding.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/foreign.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace giga_edge::vits {

namespace {

constexpr std::string_view kMagic = "#vits english 7 windows";
constexpr std::string_view kChecksum = "#adler32 ";
constexpr std::string_view kStress = " stress ";
constexpr size_t kMaxLetters = EnglishReader::kMaxLetters;
constexpr size_t kMaxContext = 9; // letters on one side of a window
constexpr size_t kMaxVowels = 7;  // the stress rules take words of more vowels as of this many
constexpr size_t kMaxPhonesPerLetter = 8;
constexpr size_t kMaxPhonesPerWord = 128;
using Occupancy = std::array<bool, kMaxLetters>;
using Windows = std::vector<std::pair<size_t, size_t>>;

enum class Phone : uint8_t {
  AA,
  AE,
  AH,
  AO,
  AW,
  AY,
  EH,
  ER,
  EY,
  IH,
  IY,
  OW,
  OY,
  UH,
  UW, // the vowels
  B,
  CH,
  D,
  DH,
  F,
  G,
  HH,
  JH,
  K,
  L,
  M,
  N,
  NG,
  P,
  R,
  S,
  SH,
  T,
  TH,
  V,
  W,
  Y,
  Z,
  ZH,
  NONE
};

constexpr std::array<std::string_view, static_cast<size_t>(Phone::NONE)> kPhoneNames = {
  "AA", "AE", "AH", "AO", "AW", "AY", "EH", "ER", "EY", "IH", "IY", "OW", "OY", "UH", "UW", "B", "CH", "D", "DH", "F",
  "G",  "HH", "JH", "K",  "L",  "M",  "N",  "NG", "P",  "R",  "S",  "SH", "T",  "TH", "V",  "W", "Y",  "Z", "ZH",
};

constexpr size_t kMaxKey = 2 * kMaxContext + 3;

bool IsVowel(Phone phone) {
  return phone <= Phone::UW;
}

bool IsConsonant(Phone phone) {
  return phone != Phone::NONE && !IsVowel(phone);
}

// A phoneme of a word and the letter it is spelled with.
struct Sound {
  Phone phone = Phone::NONE;
  int stress = 0;
  size_t letter = 0; // the index of the letter in the word
};

// "AH0" -> AH, 0; false for a name that is not an ARPAbet phoneme.
bool ParsePhone(std::string_view name, Phone& phone, int& stress) {
  stress = 0;
  const bool marked = !name.empty() && name.back() >= '0' && name.back() <= '2';
  if (marked) {
    stress = name.back() - '0';
    name.remove_suffix(1);
  }
  for (size_t i = 0; i < kPhoneNames.size(); ++i) {
    if (kPhoneNames[i] == name) {
      phone = static_cast<Phone>(i);
      return IsVowel(phone) == marked;
    }
  }
  return false;
}

// A chunk of one letter: "_" or phonemes joined by '.'.
bool ParseChunk(std::string_view chunk, size_t letter, std::vector<Sound>& sounds) {
  if (chunk == "_") {
    return true;
  }
  size_t count = 0;
  while (true) {
    if (++count > kMaxPhonesPerLetter || sounds.size() >= kMaxPhonesPerWord) {
      return false;
    }
    const auto end = chunk.find('.');
    Sound sound;
    sound.letter = letter;
    if (!ParsePhone(chunk.substr(0, end), sound.phone, sound.stress)) {
      return false;
    }
    sounds.push_back(sound);
    if (end == std::string_view::npos) {
      return true;
    }
    chunk.remove_prefix(end + 1);
  }
}

// One primary stress: the first primary one, else the first secondary one; the others become secondary. A word
// without stressed vowels ("the") stays so.
void KeepOnePrimary(std::vector<Sound>& sounds) {
  size_t primary = sounds.size();
  for (const int stress : {1, 2}) {
    for (size_t i = 0; i < sounds.size() && primary == sounds.size(); ++i) {
      if (IsVowel(sounds[i].phone) && sounds[i].stress == stress) {
        primary = i;
      }
    }
  }
  for (size_t i = 0; i < sounds.size(); ++i) {
    if (i == primary) {
      sounds[i].stress = 1;
    } else if (sounds[i].stress == 1) {
      sounds[i].stress = 2;
    }
  }
}

bool IsVowelLetter(char letter) {
  return letter == 'a' || letter == 'e' || letter == 'i' || letter == 'o' || letter == 'u' || letter == 'y';
}

// The letter that spells the vowel sounds[k]: its own letter, or else a silent vowel letter next to it, as the
// alignment may tie a vowel to a consonant beside its letter ("hello": h -> HH AH0, e -> nothing).
char VowelSpelling(const std::vector<Sound>& sounds, size_t k, std::string_view word, const Occupancy& occupied) {
  const size_t letter = sounds[k].letter;
  if (IsVowelLetter(word[letter])) {
    return word[letter];
  }
  const auto silent_vowel = [&](size_t index) {
    if (index >= word.size() || !IsVowelLetter(word[index])) {
      return false;
    }
    return !occupied[index];
  };
  if (letter > 0 && silent_vowel(letter - 1)) {
    return word[letter - 1];
  }
  if (silent_vowel(letter + 1)) {
    return word[letter + 1];
  }
  // An expansion of the preceding consonant may occupy the vowel letter: saxophone, x -> K S.
  if (
    letter > 1 && IsVowelLetter(word[letter - 1]) && k > 1 && IsConsonant(sounds[k - 1].phone)
    && sounds[k - 1].letter == letter - 1 && IsConsonant(sounds[k - 2].phone)
  ) {
    return word[letter - 1];
  }
  return word[letter];
}

std::string_view ReducedVowel(char letter) {
  switch (letter) {
    case 'a':
    case 'u':
      return "а";
    case 'o':
      return "о";
    case 'e':
      return "э";
    case 'i':
    case 'y':
      return "и";
    default:
      return ""; // no vowel letter: "apple", "google"
  }
}

// The Russian letters of the vowel sounds[k], before iotation; `russian_stressed`: Russian stresses this vowel.
std::string_view VowelLetters(
  const std::vector<Sound>& sounds,
  size_t k,
  std::string_view word,
  const Occupancy& occupied,
  bool russian_stressed = false
) {
  const auto& sound = sounds[k];
  const char letter = VowelSpelling(sounds, k, word, occupied);
  const auto phone_at = [&sounds](size_t i) {
    return i < sounds.size() ? sounds[i].phone : Phone::NONE;
  };
  const Phone previous = k > 0 ? sounds[k - 1].phone : Phone::NONE;
  switch (sound.phone) {
    case Phone::AA:
      return letter == 'o' ? "о" : "а";
    case Phone::AE:
      return russian_stressed && (sound.stress != 2 || word.substr(word.size() > 3 ? word.size() - 3 : 0) == "pad")
        ? "э"
        : "а"; // Native primary stress or the adapted -pad; -gram keeps "а".
    case Phone::AH:
      if (sound.stress != 0) {
        return "а";
      }
      if ((previous == Phone::SH || previous == Phone::ZH) && phone_at(k + 1) == Phone::N) {
        return ""; // "action" -> "+экшн", "vision" -> "в+ижн"
      }
      return ReducedVowel(letter);
    case Phone::AO:
      return "о";
    case Phone::AW:
      return "ау";
    case Phone::AY:
      return "ай";
    case Phone::EH:
      return "э";
    case Phone::ER:
      if (sound.stress == 0) {
        return letter == 'o' ? "ор" : (letter == 'a' ? "ар" : "эр");
      }
      if (letter == 'o' && previous == Phone::W) {
        return "ор"; // "world"
      }
      // "first" -> "ф+ёрст", but "were" -> "у+эр", "yearn" -> "+ерн".
      return IsConsonant(previous) && previous != Phone::W && previous != Phone::Y ? "ёр" : "эр";
    case Phone::EY:
      return "эй";
    case Phone::IH:
    case Phone::IY:
      return "и";
    case Phone::OW: {
      // Keep written ow/ou/oa under stress and ow even without it (soul, shows). A plain unstressed o stays о.
      const auto spelling = word.substr(sound.letter > 0 ? sound.letter - 1 : 0, 4);
      const bool spelled_ow = spelling.find("ow") != std::string_view::npos;
      const bool written_diphthong = spelled_ow || spelling.find("ou") != std::string_view::npos
        || spelling.find("oa") != std::string_view::npos;
      const bool plain_o = !russian_stressed && letter == 'o' && !spelled_ow;
      const Phone next = phone_at(k + 1);
      return !plain_o && (next == Phone::NONE || IsVowel(next) || written_diphthong) ? "оу" : "о";
    }
    case Phone::OY:
      return "ой";
    default: // UH, UW
      return "у";
  }
}

std::string_view ConsonantLetters(const std::vector<Sound>& sounds, size_t k) {
  const Phone previous = k > 0 ? sounds[k - 1].phone : Phone::NONE;
  const Phone next = k + 1 < sounds.size() ? sounds[k + 1].phone : Phone::NONE;
  switch (sounds[k].phone) {
    case Phone::B:
      return "б";
    case Phone::CH:
      return "ч";
    case Phone::D:
      return "д";
    case Phone::DH:
    case Phone::Z:
      return "з";
    case Phone::F:
      return "ф";
    case Phone::G:
      return "г";
    case Phone::HH:
      return "х";
    case Phone::JH:
      return "дж";
    case Phone::K:
      return "к";
    case Phone::L:
      return "л";
    case Phone::M:
      return "м";
    case Phone::N:
      return "н";
    case Phone::NG:
      return next == Phone::K || next == Phone::G ? "н" : "нг";
    case Phone::P:
      return "п";
    case Phone::R:
      return "р";
    case Phone::S:
    case Phone::TH:
      return "с";
    case Phone::SH:
      return "ш";
    case Phone::T:
      return "т";
    case Phone::V:
      return "в";
    case Phone::W:
      // "queen" -> "кв+ин", "wood" -> "в+уд", but "weekend" -> "уик+энд".
      return IsConsonant(previous) || next == Phone::UW || next == Phone::UH || !IsVowel(next) ? "в" : "у";
    case Phone::Y:
      return "й"; // before a vowel it is written by the vowel
    default:      // ZH
      return "ж";
  }
}

// The iotated form of the first vowel letter: "а" -> "я" after 'y' ("yahoo" -> "яху") or after 'l' for 'u'
// ("blue" -> "блю"). `prefix` gets "ь" after a consonant ("music" -> "мьюзик") or "й" where a letter has no iotated
// form ("yield" -> "йилд").
std::string_view Iotated(std::string_view letters, bool after_consonant, bool stressed, std::string_view& prefix) {
  prefix = after_consonant ? "ь" : "";
  const auto first = letters.substr(0, 2);
  if (first == "а") {
    return "я";
  }
  if (first == "у") {
    return "ю";
  }
  if (first == "э") {
    return "е";
  }
  if (first == "о" && stressed) {
    return "ё";
  }
  prefix = after_consonant ? "ь" : "й"; // "и", an unstressed "о", "ё" of "ёр"
  return first;
}

bool EndsWith(std::string_view word, std::string_view ending) {
  return word.size() >= ending.size() && word.substr(word.size() - ending.size()) == ending;
}

std::string_view Find(std::string_view entries, std::string_view key);

// Whole-word entries override English stress, including plurals. Otherwise a listed final part after a dictionary
// word needs secondary stress and a closing consonant other than s/z; -ate/-ize/-ise/-yze/-ite/-ide are excluded.
bool StressedOnLastPart(
  std::string_view entries, const std::vector<Sound>& sounds, size_t last, std::string_view word
) {
  bool consonant_after = false;
  for (size_t i = last + 1; i < sounds.size(); ++i) {
    consonant_after = consonant_after || (sounds[i].phone != Phone::S && sounds[i].phone != Phone::Z);
  }
  auto stem = word;
  if (stem.size() > 1 && stem.back() == 's') {
    stem.remove_suffix(1);
  }
  const auto whole = Find(entries, ">" + std::string(stem));
  if (whole == "0" || whole == "1") {
    return whole == "1";
  }
  if (!consonant_after || sounds[last].stress != 2) {
    return false;
  }
  for (const std::string_view suffix : {"ate", "ize", "ise", "yze", "ite", "ide"}) {
    if (EndsWith(stem, suffix)) {
      return false;
    }
  }
  std::array<char, kMaxLetters + 1> key{};
  key[0] = '>';
  for (size_t start = 1; start < stem.size(); ++start) {
    const auto part = stem.substr(start);
    part.copy(key.data() + 1, part.size());
    if (
      Find(entries, std::string_view(key.data(), part.size() + 1)) == "1"
      && !Find(entries, stem.substr(0, start)).empty()
    ) {
      return true;
    }
  }
  return false;
}

// Where Russian puts the stress of an English word: as English does, but on the last part of a compound (see
// StressedOnLastPart); a word without a stressed vowel takes its first written vowel ("the").
size_t RussianStress(
  std::string_view entries, const std::vector<Sound>& sounds, std::string_view word, const Occupancy& occupied
) {
  size_t primary = sounds.size();
  size_t first = sounds.size();
  size_t last = sounds.size();
  for (size_t i = 0; i < sounds.size(); ++i) {
    if (!IsVowel(sounds[i].phone)) {
      continue;
    }
    last = i;
    if (sounds[i].stress == 1) {
      primary = i;
    }
    if (first == sounds.size() && !VowelLetters(sounds, i, word, occupied).empty()) {
      first = i;
    }
  }
  if (primary == sounds.size()) {
    return first;
  }
  return StressedOnLastPart(entries, sounds, last, word) ? last : primary;
}

std::string Russian(std::string_view entries, const std::vector<Sound>& sounds, std::string_view word) {
  Occupancy occupied{};
  for (const auto& sound : sounds) {
    occupied[sound.letter] = true;
  }
  const size_t stressed = RussianStress(entries, sounds, word, occupied);
  std::string result;
  for (size_t k = 0; k < sounds.size(); ++k) {
    const Phone phone = sounds[k].phone;
    const Phone previous = k > 0 ? sounds[k - 1].phone : Phone::NONE;
    if (!IsVowel(phone)) {
      const bool iotation_follows = phone == Phone::Y && k + 1 < sounds.size() && IsVowel(sounds[k + 1].phone);
      if (!iotation_follows) {
        result += ConsonantLetters(sounds, k);
      }
      continue;
    }
    auto letters = VowelLetters(sounds, k, word, occupied, k == stressed);
    if (letters.empty()) {
      continue;
    }
    const Phone before_previous = k >= 2 ? sounds[k - 2].phone : Phone::NONE;
    // Preserve the established blue/bluetooth spelling; other l + UW combinations stay hard (glue, flute).
    const bool after_short_i = before_previous == Phone::AY || before_previous == Phone::EY
      || before_previous == Phone::OY;
    std::string_view prefix;
    std::string_view first = letters.substr(0, 2);
    if (
      (previous == Phone::Y && !after_short_i)
      || (previous == Phone::L && phone == Phone::UW && before_previous == Phone::B)
    ) {
      first = Iotated(letters, previous == Phone::Y && IsConsonant(before_previous), k == stressed, prefix);
    }
    result += prefix;
    if (k == stressed) {
      result += '+';
    }
    result += first;
    result += letters.substr(2);
  }
  return result;
}

// A binary search over the lines of the file after the header: `low` and `high` are line starts. Every line has a
// tab and the keys are sorted: the constructor checks it.
std::string_view Find(std::string_view entries, std::string_view key) {
  size_t low = 0;
  size_t high = entries.size();
  while (low < high) {
    size_t start = low + (high - low) / 2;
    while (start > low && entries[start - 1] != '\n') {
      --start;
    }
    const auto line = entries.substr(start, entries.find('\n', start) - start);
    const auto tab = line.find('\t');
    const int order = line.substr(0, tab).compare(key);
    if (order == 0) {
      return line.substr(tab + 1);
    }
    if (order < 0) {
      low = start + line.size() + 1;
    } else {
      high = start;
    }
  }
  return {};
}

bool IsRussianText(std::string_view text) {
  if (text.empty() || text.size() % 2 != 0) {
    return false;
  }
  for (size_t i = 0; i < text.size(); i += 2) {
    const auto first = static_cast<unsigned char>(text[i]);
    const auto second = static_cast<unsigned char>(text[i + 1]);
    if (!((first == 0xd0 && second >= 0xb0 && second <= 0xbf)
          || (first == 0xd1 && ((second >= 0x80 && second <= 0x8f) || second == 0x91)))) {
      return false;
    }
  }
  return true;
}

bool IsRussianVowel(std::string_view letter) {
  for (const std::string_view vowel : {"а", "е", "ё", "и", "о", "у", "ы", "э", "ю", "я"}) {
    if (letter == vowel) {
      return true;
    }
  }
  return false;
}

bool IsTraditionalReading(std::string_view text) {
  bool stress = false;
  bool letters = false;
  for (size_t pos = 0; pos < text.size();) {
    if (text[pos] == '+') {
      if (stress || !IsRussianVowel(text.substr(pos + 1, 2))) {
        return false;
      }
      stress = true;
      ++pos;
    } else if (text[pos] == '-' || text[pos] == ' ') {
      const size_t next = pos + 1 + (pos + 1 < text.size() && text[pos + 1] == '+' ? 1 : 0);
      if (!letters || !IsRussianText(text.substr(next, 2))) {
        return false;
      }
      letters = false;
      ++pos;
    } else {
      if (!IsRussianText(text.substr(pos, 2))) {
        return false;
      }
      letters = true;
      pos += 2;
    }
  }
  return stress && letters;
}

struct Accent {
  std::string spelling;
  size_t letters = 1;
};
using Accents = std::array<Accent, kMaxLetters>;

constexpr std::array<std::pair<size_t, size_t>, 9> kWindows = {
  {{4, 4}, {3, 4}, {3, 3}, {2, 3}, {2, 2}, {1, 2}, {1, 1}, {0, 1}, {0, 0}}
};

constexpr std::array<std::pair<size_t, size_t>, 10> kAccentWindows = {
  {{4, 4}, {3, 4}, {3, 3}, {2, 3}, {2, 2}, {1, 2}, {1, 1}, {1, 0}, {0, 1}, {0, 0}}
};

// Practical transcription uses its own fixed nested windows, independently of the phoneme model's window size.
std::string ReadName(std::string_view entries, std::string_view word, const Accents& accents) {
  std::string result;
  for (size_t i = 0; i < word.size();) {
    size_t consumed = 0;
    if (!accents[i].spelling.empty()) {
      for (const auto& [left, right] : kAccentWindows) {
        std::string key = "~";
        for (size_t offset = left; offset > 0; --offset) {
          key += i >= offset ? word[i - offset] : '^';
        }
        key += '[';
        key += accents[i].spelling;
        key += ']';
        for (size_t offset = 0; offset < right; ++offset) {
          const size_t next = i + accents[i].letters + offset;
          key += next < word.size() ? word[next] : '$';
        }
        const auto value = Find(entries, key);
        if (value == "_" || IsRussianText(value)) {
          if (value != "_") {
            result += value;
          }
          consumed = accents[i].letters;
          break;
        }
      }
    }
    for (size_t size = std::min<size_t>(4, word.size() - i); size > 0 && consumed == 0; --size) {
      bool marked = false;
      for (size_t offset = 0; size > 1 && offset < size; ++offset) {
        marked = marked || !accents[i + offset].spelling.empty();
      }
      if (marked) {
        continue;
      }
      for (const auto& [left, right] : kWindows) {
        std::string key = "@";
        for (size_t offset = left; offset > 0; --offset) {
          key += i >= offset ? word[i - offset] : '^';
        }
        key += '[';
        key += word.substr(i, size);
        key += ']';
        for (size_t offset = 0; offset < right; ++offset) {
          key += i + size + offset < word.size() ? word[i + size + offset] : '$';
        }
        const auto value = Find(entries, key);
        if (value == "_" || IsRussianText(value)) {
          if (value != "_") {
            result += value;
          }
          consumed = size;
          break;
        }
      }
    }
    i += consumed == 0 ? 1 : consumed;
  }
  std::vector<size_t> vowels;
  for (size_t i = 0; i < result.size(); i += 2) {
    if (IsRussianVowel(std::string_view(result).substr(i, 2))) {
      vowels.push_back(i);
    }
  }
  if (vowels.empty()) {
    return result;
  }
  size_t from_end = IsRussianVowel(std::string_view(result).substr(result.size() - 2)) ? 1 : 0;
  for (size_t length = std::min(word.size(), kMaxContext); length > 0; --length) {
    const auto stress = Find(entries, "!" + std::string(word.substr(word.size() - length)));
    if (stress == "F") {
      from_end = vowels.size() - 1;
      break;
    }
    if (stress.size() == 1 && stress[0] >= '0' && stress[0] <= '9') {
      from_end = static_cast<size_t>(stress[0] - '0');
      break;
    }
  }
  size_t stressed = vowels[vowels.size() - 1 - std::min(from_end, vowels.size() - 1)];
  for (const auto vowel : vowels) {
    if (result.compare(vowel, 2, "ё") == 0) {
      stressed = vowel;
      break;
    }
  }
  result.insert(stressed, "+");
  return result;
}

struct Fold {
  std::string_view letters;
  std::string_view ascii;
};
constexpr Fold kFolds[] = {
  {"ÀÁÂÃÄÅĀĂĄ", "A"},  {"àáâãäåāăą", "a"},  {"ÇĆĈĊČ", "C"}, {"çćĉċč", "c"}, {"ÐĎĐ", "D"},       {"ðďđ", "d"},
  {"ÈÉÊËĒĔĖĘĚ", "E"},  {"èéêëēĕėęě", "e"},  {"ĜĞĠĢ", "G"},  {"ĝğġģ", "g"},  {"ĤĦ", "H"},        {"ĥħ", "h"},
  {"ÌÍÎÏĨĪĬĮİ", "I"},  {"ìíîïĩīĭįı", "i"},  {"Ĵ", "J"},     {"ĵ", "j"},     {"Ķ", "K"},         {"ķ", "k"},
  {"ĹĻĽŁ", "L"},       {"ĺļľł", "l"},       {"ÑŃŅŇ", "N"},  {"ñńņň", "n"},  {"ÒÓÔÕÖØŌŎŐ", "O"}, {"òóôõöøōŏő", "o"},
  {"ŔŖŘ", "R"},        {"ŕŗř", "r"},        {"ŚŜŞŠ", "S"},  {"śŝşš", "s"},  {"ŢŤŦ", "T"},       {"ţťŧ", "t"},
  {"ÙÚÛÜŨŪŬŮŰŲ", "U"}, {"ùúûüũūŭůűų", "u"}, {"Ŵ", "W"},     {"ŵ", "w"},     {"ÝŶŸ", "Y"},       {"ýŷÿ", "y"},
  {"ŹŻŽ", "Z"},        {"źżž", "z"},        {"Æ", "AE"},    {"æ", "ae"},    {"Œ", "OE"},        {"œ", "oe"},
  {"Þ", "TH"},         {"þ", "th"},         {"ẞ", "SS"},    {"ß", "ss"}
};

// Latin diacritics are folded for lookup, retaining case for the origin model. No locale or Unicode dependency.
bool Lower(std::string_view word, std::string& lower, std::string* casing = nullptr, Accents* accents = nullptr) {
  lower.clear();
  if (accents != nullptr) {
    *accents = {};
  }
  size_t last_start = 0;
  if (casing != nullptr) {
    casing->clear();
  }
  if (word.size() > 3 * kMaxLetters) {
    return false;
  }
  bool has_letter = false;
  for (size_t i = 0; i < word.size();) {
    const auto byte = static_cast<unsigned char>(word[i]);
    std::string_view folded = word.substr(i, 1);
    size_t size = 1;
    std::string_view accent;
    if (byte >= 128) {
      size = byte >= 0xc2 && byte <= 0xdf ? 2 : (byte >= 0xe0 && byte <= 0xef ? 3 : 0);
      if (size == 0 || i + size > word.size()) {
        return false;
      }
      const auto symbol = word.substr(i, size);
      // Decomposed accents follow a Latin letter; they never contribute another letter or a capital.
      if (
        symbol == "̀" || symbol == "́" || symbol == "̂" || symbol == "̃" || symbol == "̈" || symbol == "̊" || symbol == "̌"
        || symbol == "̄" || symbol == "̧" || symbol == "̨" || symbol == "̆" || symbol == "̇" || symbol == "̋"
      ) {
        if (lower.empty() || lower.back() == '\'') {
          return false;
        }
        if (accents != nullptr) {
          auto& previous = (*accents)[last_start];
          if (previous.spelling.empty()) {
            previous.spelling = lower.substr(last_start);
          }
          previous.spelling += symbol;
        }
        i += size;
        continue;
      }
      folded = {};
      for (size_t index = 0; index < std::size(kFolds); ++index) {
        const auto& fold = kFolds[index];
        const auto offset = fold.letters.find(symbol);
        if (offset != std::string_view::npos) {
          folded = fold.ascii;
          accent = folded.front() >= 'A' && folded.front() <= 'Z'
            ? kFolds[index + 1].letters.substr(offset, 2)
            : symbol;
          break;
        }
      }
      if (folded.empty()) {
        return false;
      }
    }
    last_start = lower.size();
    for (const char letter : folded) {
      if (letter >= 'A' && letter <= 'Z') {
        lower += static_cast<char>(letter - 'A' + 'a');
      } else if ((letter >= 'a' && letter <= 'z') || letter == '\'') {
        lower += letter;
      } else {
        return false;
      }
      if (lower.size() > kMaxLetters) {
        return false;
      }
      has_letter = has_letter || letter != '\'';
      if (casing != nullptr) {
        *casing += letter;
      }
    }
    if (accents != nullptr && !accent.empty()) {
      (*accents)[last_start] = {std::string(accent), folded.size()};
    }
    i += size;
  }
  return has_letter;
}

// The dictionary entry of a lower-case word: one chunk per letter; false for a missing or a damaged one.
bool FromDictionary(std::string_view entries, std::string_view word, std::vector<Sound>& sounds) {
  sounds.clear();
  auto chunks = Find(entries, word);
  size_t letter = 0;
  while (!chunks.empty() && letter < word.size()) {
    const auto end = chunks.find(' ');
    if (!ParseChunk(chunks.substr(0, end), letter, sounds)) {
      sounds.clear();
      return false;
    }
    ++letter;
    chunks = end == std::string_view::npos ? std::string_view() : chunks.substr(end + 1);
  }
  if (letter != word.size() || !chunks.empty()) {
    sounds.clear();
    return false;
  }
  return true;
}

bool ParseScore(std::string_view text, int& score);

bool HasWordSupport(std::string_view entries, std::string_view word) {
  int count = 0;
  return ParseScore(Find(entries, "&" + std::string(word)), count) && count >= 2;
}

bool IsVoiceless(Phone phone) {
  return phone == Phone::P || phone == Phone::T || phone == Phone::K || phone == Phone::F || phone == Phone::TH
    || phone == Phone::S || phone == Phone::SH || phone == Phone::CH || phone == Phone::HH;
}

bool IsSibilant(Phone phone) {
  return phone == Phone::S || phone == Phone::Z || phone == Phone::SH || phone == Phone::ZH || phone == Phone::CH
    || phone == Phone::JH;
}

// Only one inflection is removed; a dictionary pronunciation supplies the stem's vowels and stress.
bool FromDerived(std::string_view entries, std::string_view word, std::vector<Sound>& sounds, bool& sure) {
  std::vector<Sound> fallback;
  int best_support = -1;
  for (const std::string_view suffix : {"'s", "ers", "ing", "ies", "ied", "es", "ed", "er", "ly", "s"}) {
    if (!EndsWith(word, suffix) || word.size() < suffix.size() + 2) {
      continue;
    }
    const auto stem = word.substr(0, word.size() - suffix.size());
    std::vector<std::string> candidates;
    const bool vowel_suffix = suffix == "ing" || suffix == "ed" || suffix == "er" || suffix == "ers";
    if (suffix == "ies" || suffix == "ied") {
      candidates.emplace_back(std::string(stem) + "y");
    } else {
      if (vowel_suffix && stem.back() != 'e') {
        candidates.emplace_back(std::string(stem) + "e");
      }
      candidates.emplace_back(stem);
      if (
        vowel_suffix && stem.size() > 3 && stem.back() == stem[stem.size() - 2] && !IsVowelLetter(stem.back())
        && IsVowelLetter(stem[stem.size() - 3])
      ) {
        candidates.emplace_back(stem.substr(0, stem.size() - 1));
      }
    }
    for (const auto& base : candidates) {
      std::vector<Sound> result;
      if (base.size() < 2 || !FromDictionary(entries, base, result) || result.empty()) {
        continue;
      }
      // Restored e must be silent; otherwise a surname such as Hoppe would add a spurious syllable.
      if (
        base.size() > stem.size() && base.back() == 'e'
        && std::any_of(result.begin(), result.end(), [&base](const Sound& sound) {
             return sound.letter == base.size() - 1;
           })
      ) {
        continue;
      }
      for (auto& sound : result) {
        sound.letter = std::min(sound.letter, stem.size() - 1);
      }
      const Phone last = result.back().phone;
      const auto append = [&](Phone phone, size_t offset = 0) {
        result.push_back({phone, 0, std::min(stem.size() + offset, word.size() - 1)});
      };
      if (suffix == "ed" || suffix == "ied") {
        if (last == Phone::T || last == Phone::D) {
          append(Phone::IH);
        }
        append(
          last == Phone::T || last == Phone::D ? Phone::D : (IsVoiceless(last) ? Phone::T : Phone::D), suffix.size() - 1
        );
      } else if (suffix == "ing") {
        append(Phone::IH);
        append(Phone::NG, 1);
      } else if (suffix == "ly") {
        append(Phone::L);
        append(Phone::IY, 1);
      } else if (suffix == "er" || suffix == "ers") {
        append(Phone::ER);
        if (suffix == "ers") {
          append(Phone::Z, 2);
        }
      } else {
        if (IsSibilant(last)) {
          append(Phone::IH);
        }
        append(IsSibilant(last) ? Phone::Z : (IsVoiceless(last) ? Phone::S : Phone::Z), suffix.size() - 1);
      }
      if (result.size() > kMaxPhonesPerWord) {
        continue;
      }
      int support = 0;
      ParseScore(Find(entries, "&" + base), support);
      if (support > best_support) {
        fallback = std::move(result);
        best_support = support;
      }
    }
  }
  sounds = std::move(fallback);
  sure = best_support >= 2;
  return !sounds.empty();
}

// Prefer two parts to three, then a longer shortest part. Every part must have a usable dictionary pronunciation.
bool FromCompound(std::string_view entries, std::string_view word, std::vector<Sound>& sounds, bool& sure) {
  std::vector<Sound> best;
  size_t best_shortest = 0;
  for (const bool require_support : {true, false}) {
    for (size_t parts = 2; parts <= 3 && best.empty(); ++parts) {
      for (size_t first = 3; first + 3 * (parts - 1) <= word.size(); ++first) {
        const size_t second_begin = parts == 2 ? word.size() : first + 3;
        const size_t second_end = parts == 2 ? word.size() : word.size() - 3;
        for (size_t second = second_begin; second <= second_end; ++second) {
          const std::array<size_t, 4> ends = {0, first, second, word.size()};
          std::vector<Sound> result;
          size_t shortest = word.size();
          bool valid = true;
          for (size_t part = 0; part < parts; ++part) {
            const size_t length = ends[part + 1] - ends[part];
            const auto spelling = word.substr(ends[part], length);
            std::vector<Sound> chunk;
            if (
              (require_support && !HasWordSupport(entries, spelling)) || !FromDictionary(entries, spelling, chunk)
              || chunk.empty()
            ) {
              valid = false;
              break;
            }
            shortest = std::min(shortest, length);
            for (auto sound : chunk) {
              sound.letter += ends[part];
              result.push_back(sound);
            }
          }
          if (valid && result.size() <= kMaxPhonesPerWord && shortest > best_shortest) {
            best_shortest = shortest;
            best = std::move(result);
          }
        }
      }
    }
    if (!best.empty()) {
      sure = require_support;
      break;
    }
  }
  sounds = std::move(best);
  return !sounds.empty();
}

bool FromStructure(std::string_view entries, std::string_view word, std::vector<Sound>& sounds, bool& sure) {
  std::vector<Sound> derived;
  bool derived_sure = false;
  const bool has_derived = FromDerived(entries, word, derived, derived_sure);
  if (has_derived && derived_sure) {
    sounds = std::move(derived);
    sure = true;
    return true;
  }
  std::vector<Sound> compound;
  bool compound_sure = false;
  const bool has_compound = FromCompound(entries, word, compound, compound_sure);
  if (has_compound && (!has_derived || compound_sure)) {
    sounds = std::move(compound);
    sure = compound_sure;
    return true;
  }
  sounds = std::move(derived);
  sure = derived_sure;
  return has_derived;
}

// Fixed-point scores avoid locale-dependent parsing and bound the sum even for an unchecked damaged file.
bool ParseScore(std::string_view text, int& score) {
  const bool negative = !text.empty() && text.front() == '-';
  if (negative) {
    text.remove_prefix(1);
  }
  if (text.empty() || text.size() > 5) {
    return false;
  }
  score = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return false;
    }
    score = score * 10 + c - '0';
  }
  if (score > 32000) {
    return false;
  }
  score = negative ? -score : score;
  return true;
}

int OriginScore(std::string_view entries, std::string_view word, std::string_view original) {
  const auto value = [&](const std::string& key) {
    int score = 0;
    return ParseScore(Find(entries, key), score) ? score : 0;
  };
  const std::string padded = "^" + std::string(word) + "$";
  int score = 0;
  for (size_t length = 2; length <= 4; ++length) {
    for (size_t i = 0; i + length <= padded.size(); ++i) {
      score += value("%" + padded.substr(i, length));
    }
  }
  if (original.front() >= 'A' && original.front() <= 'Z') {
    score += value("%:initial");
  }
  if (std::any_of(original.begin() + 1, original.end(), [](char c) {
        return c >= 'A' && c <= 'Z';
      })) {
    score += value("%:internal");
  }
  return score;
}

bool IsEnglishByOrigin(std::string_view entries, std::string_view word, std::string_view original) {
  int threshold = 0;
  return ParseScore(Find(entries, "%:threshold"), threshold) && OriginScore(entries, word, original) >= threshold;
}

// The letter rules for every letter of a lower-case word: the widest window found.
bool FromRules(std::string_view entries, const Windows& windows, std::string_view word, std::vector<Sound>& sounds) {
  std::array<char, kMaxKey> key{};
  for (size_t i = 0; i < word.size(); ++i) {
    for (const auto& [left, right] : windows) {
      size_t size = 0;
      for (size_t offset = left; offset > 0; --offset) {
        key[size++] = i >= offset ? word[i - offset] : '^';
      }
      key[size++] = '[';
      key[size++] = word[i];
      key[size++] = ']';
      for (size_t offset = 1; offset <= right; ++offset) {
        key[size++] = i + offset < word.size() ? word[i + offset] : '$';
      }
      const auto chunk = Find(entries, std::string_view(key.data(), size));
      if (!chunk.empty()) {
        if (!ParseChunk(chunk, i, sounds)) {
          sounds.clear();
          return false;
        }
        break;
      }
    }
  }
  return true;
}

// The stress rule of the longest ending found among the words of as many vowels; nothing changes without one.
void StressByEnding(std::string_view entries, size_t max_ending, std::string_view word, std::vector<Sound>& sounds) {
  size_t vowels = 0;
  for (const auto& sound : sounds) {
    vowels += IsVowel(sound.phone) ? 1 : 0;
  }
  if (vowels == 0) {
    return;
  }
  std::array<char, 2 + kMaxContext> key{};
  key[0] = static_cast<char>('0' + std::min(vowels, kMaxVowels));
  key[1] = ':';
  for (size_t ending = std::min(max_ending, word.size()) + 1; ending-- > 0;) {
    word.substr(word.size() - ending).copy(key.data() + 2, ending);
    const auto answer = Find(entries, std::string_view(key.data(), 2 + ending));
    if (answer.empty()) {
      continue;
    }
    if (answer.size() != 1 || answer[0] < '0' || answer[0] > '9' || static_cast<size_t>(answer[0] - '0') >= vowels) {
      return;
    }
    const size_t stressed = vowels - 1 - static_cast<size_t>(answer[0] - '0');
    size_t vowel = 0;
    for (auto& sound : sounds) {
      if (!IsVowel(sound.phone)) {
        continue;
      }
      if (vowel++ == stressed) {
        sound.stress = 1;
      } else if (sound.stress == 1) {
        sound.stress = 2;
      }
    }
    return;
  }
}

// The phonemes of a lower-case word by the letter and the stress rules.
bool SpellByRules(
  std::string_view entries,
  const Windows& windows,
  size_t stress_ending,
  std::string_view word,
  std::vector<Sound>& sounds
) {
  sounds.clear();
  if (!FromRules(entries, windows, word, sounds)) {
    return false;
  }
  StressByEnding(entries, stress_ending, word, sounds);
  return true;
}

// "#adler32 0123abcd" -> the number; false on another line.
bool ParseChecksum(std::string_view line, uint32_t& checksum) {
  if (line.substr(0, kChecksum.size()) != kChecksum || line.size() != kChecksum.size() + 8) {
    return false;
  }
  checksum = 0;
  for (const char symbol : line.substr(kChecksum.size())) {
    const int digit = symbol >= '0' && symbol <= '9'
      ? symbol - '0'
      : (symbol >= 'a' && symbol <= 'f' ? symbol - 'a' + 10 : -1);
    if (digit < 0) {
      return false;
    }
    checksum = (checksum << 4) | static_cast<uint32_t>(digit);
  }
  return true;
}

bool IsLetters(std::string_view text) {
  return std::all_of(text.begin(), text.end(), [](char symbol) {
    return (symbol >= 'a' && symbol <= 'z') || symbol == '\'';
  });
}

bool IsRuleKey(std::string_view key, size_t max_context = kMaxContext, size_t max_spelling = 1) {
  const auto open = key.find('[');
  const auto close = key.find(']');
  if (
    open == std::string_view::npos || close == std::string_view::npos || open > max_context || close <= open + 1
    || close - open - 1 > max_spelling || !IsLetters(key.substr(open + 1, close - open - 1))
    || key.size() - close - 1 > max_context
  ) {
    return false;
  }
  auto left = key.substr(0, open);
  auto right = key.substr(close + 1);
  while (!left.empty() && left.front() == '^') {
    left.remove_prefix(1);
  }
  while (!right.empty() && right.back() == '$') {
    right.remove_suffix(1);
  }
  return IsLetters(left) && IsLetters(right);
}

bool IsAccentRule(std::string_view key) {
  const auto open = key.find('[');
  const auto close = key.find(']');
  if (open == std::string_view::npos || close == std::string_view::npos || close <= open + 1) {
    return false;
  }
  const auto symbol = key.substr(open + 1, close - open - 1);
  if (symbol.size() < 2 || symbol.size() > 3) {
    return false;
  }
  std::string lower;
  Accents accents;
  if (
    !Lower(symbol, lower, nullptr, &accents) || lower.size() > 2 || accents[0].spelling != symbol
    || accents[0].letters != lower.size()
  ) {
    return false;
  }
  return IsRuleKey(std::string(key.substr(0, open + 1)) + "a" + std::string(key.substr(close)), 4, 1);
}

bool IsEntry(std::string_view key, std::string_view value, size_t stress_ending) {
  std::vector<Sound> sounds;
  if (key[0] == '=') {
    return key.size() > 1 && key.size() <= kMaxLetters + 1 && IsLetters(key.substr(1))
      && std::any_of(
             key.begin() + 1,
             key.end(),
             [](char c) {
               return c >= 'a' && c <= 'z';
             }
      )
      && value.size() > 2 && (value[0] == 'A' || value[0] == 'U' || value[0] == 'N') && value[1] == ' '
      && IsTraditionalReading(value.substr(2));
  }
  if (key[0] == '&') {
    int count = 0;
    return key.size() >= 3 && key.size() <= kMaxLetters + 1 && IsLetters(key.substr(1))
      && key.find('\'') == std::string_view::npos && !value.empty() && value.size() <= 3 && value.front() >= '1'
      && value.front() <= '9' && ParseScore(value, count) && count > 0 && count < 1000;
  }
  if (key[0] == '%') {
    int score = 0;
    if (!ParseScore(value, score)) {
      return false;
    }
    if (key == "%:threshold" || key == "%:initial" || key == "%:internal") {
      return true;
    }
    auto gram = key.substr(1);
    if (gram.size() < 2 || gram.size() > 4) {
      return false;
    }
    if (gram.front() == '^') {
      gram.remove_prefix(1);
    }
    if (!gram.empty() && gram.back() == '$') {
      gram.remove_suffix(1);
    }
    return !gram.empty() && IsLetters(gram);
  }
  if (key[0] == '~') {
    return IsAccentRule(key.substr(1)) && (value == "_" || IsRussianText(value));
  }
  if (key[0] == '@') {
    return IsRuleKey(key.substr(1), 4, 4) && (value == "_" || IsRussianText(value));
  }
  if (key[0] == '!') {
    return key.size() > 1 && key.size() <= kMaxContext + 1 && IsLetters(key.substr(1)) && value.size() == 1
      && ((value[0] >= '0' && value[0] <= '9') || value == "F");
  }
  if (key[0] == '>') {
    return key.size() > 1 && IsLetters(key.substr(1)) && (value == "0" || value == "1");
  }
  if (key.size() >= 2 && key[1] == ':') {
    return key[0] >= '1' && key[0] <= '7' && key.size() - 2 <= stress_ending && IsLetters(key.substr(2))
      && value.size() == 1 && value[0] >= '0' && value[0] <= '9' && (key[0] == '7' || value[0] < key[0]);
  }
  if (key.find('[') != std::string_view::npos) {
    return IsRuleKey(key) && ParseChunk(value, 0, sounds);
  }
  if (key.size() > kMaxLetters || !IsLetters(key)) {
    return false;
  }
  size_t letters = 0;
  for (size_t pos = 0; pos <= value.size(); ++letters) {
    const auto end = std::min(value.find(' ', pos), value.size());
    if (!ParseChunk(value.substr(pos, end - pos), letters, sounds)) {
      return false;
    }
    pos = end + 1;
  }
  return letters == key.size();
}

// "#vits english 7 windows 33 23 ... 00 stress 4" -> the windows and the longest ending; false on a bad header.
bool ParseHeader(std::string_view header, Windows& windows, size_t& stress_ending) {
  if (header.substr(0, kMagic.size()) != kMagic) {
    return false;
  }
  header.remove_prefix(kMagic.size());
  const auto digit = [](char symbol) {
    return symbol >= '0' && symbol <= '9';
  };
  while (header.size() >= 3 && header[0] == ' ' && digit(header[1]) && digit(header[2])
         && (header.size() == 3 || header[3] == ' ')) {
    const std::pair<size_t, size_t> window(header[1] - '0', header[2] - '0');
    if (!windows.empty() && (window.first > windows.back().first || window.second > windows.back().second)) {
      return false;
    }
    windows.push_back(window);
    header.remove_prefix(3);
  }
  if (
    windows.empty() || header.size() != kStress.size() + 1 || header.substr(0, kStress.size()) != kStress
    || !digit(header.back())
  ) {
    return false;
  }
  stress_ending = static_cast<size_t>(header.back() - '0');
  return true;
}

} // namespace

EnglishReader::EnglishReader(IDataReader& data_reader, const std::string& path, bool verify)
  : EnglishReader(data_reader.ReadPersistentData(path), path, verify) {}

EnglishReader::EnglishReader(IDataSpanPtr data, const std::string& name, bool verify)
  : span_(std::move(data)) {
  const auto fail = [&name](const std::string& what) {
    throw std::runtime_error("vits: English pronunciations '" + name + "': " + what);
  };
  if (!span_) {
    fail("no data");
  }
  const auto text = span_->View();
  if (text.substr(0, kMagic.size()) != kMagic) {
    fail("not an English pronunciation file");
  }
  // The checksum line ends the file: a file cut anywhere, at a line end too, has none.
  const auto last_line = text.size() >= 2 ? text.rfind('\n', text.size() - 2) : std::string_view::npos;
  uint32_t checksum = 0;
  if (
    text.back() != '\n' || last_line == std::string_view::npos
    || !ParseChecksum(text.substr(last_line + 1, text.size() - last_line - 2), checksum)
  ) {
    fail("the file is truncated");
  }
  const auto header_end = text.find('\n');
  if (!ParseHeader(text.substr(0, header_end), windows_, stress_ending_)) {
    fail("a bad header");
  }
  entries_ = text.substr(header_end + 1, last_line - header_end);
  if (!verify) {
    return;
  }
  if (Adler32(text.substr(0, last_line + 1)) != checksum) {
    fail("the file is damaged: wrong checksum");
  }
  std::string_view previous;
  for (size_t pos = 0, line = 2; pos < entries_.size(); ++line) {
    const auto end = entries_.find('\n', pos);
    const auto entry = entries_.substr(pos, end - pos);
    const auto tab = entry.find('\t');
    if (tab == 0 || tab == std::string_view::npos) {
      fail("line " + std::to_string(line) + " has no key");
    }
    const auto key = entry.substr(0, tab);
    if (pos > 0 && !(previous < key)) {
      fail("line " + std::to_string(line) + " is out of order");
    }
    if (!IsEntry(key, entry.substr(tab + 1), stress_ending_)) {
      fail("line " + std::to_string(line) + " is damaged");
    }
    previous = key;
    pos = end + 1;
  }
}

std::unique_ptr<EnglishReader> EnglishReader::Open(IDataReader& data_reader, const std::string& path, bool verify) {
  if (!data_reader.Exists(path)) {
    return nullptr;
  }
  return std::make_unique<EnglishReader>(data_reader, path, verify);
}

std::string EnglishReader::Read(std::string_view word, Mode mode) const {
  if (word.find('-') != std::string_view::npos) {
    if (word.size() > 3 * kMaxLetters) {
      return {};
    }
    std::string result;
    size_t letters = 0;
    size_t begin = 0;
    while (true) {
      const size_t end = word.find('-', begin);
      const auto part = word.substr(begin, end == std::string_view::npos ? end : end - begin);
      std::string folded;
      if (!Lower(part, folded)) {
        return {};
      }
      letters += folded.size();
      if (letters > kMaxLetters) {
        return {};
      }
      const auto reading = Read(part, mode);
      if (reading.empty()) {
        return {};
      }
      if (!result.empty()) {
        result += '-';
      }
      result += reading;
      if (end == std::string_view::npos) {
        return result;
      }
      begin = end + 1;
    }
  }
  const bool phrase = word.find(' ') != std::string_view::npos;
  std::string joined;
  if (phrase) {
    if (
      word.size() > 3 * kMaxLetters || word.front() == ' ' || word.back() == ' '
      || word.find("  ") != std::string_view::npos
    ) {
      return {};
    }
    for (const char symbol : word) {
      if (symbol != ' ') {
        joined += symbol;
      }
    }
    word = joined;
  }
  std::string lower;
  std::string casing;
  Accents accents;
  if (!Lower(word, lower, &casing, &accents)) {
    return {};
  }
  if (mode == Mode::NAME || (casing.front() >= 'A' && casing.front() <= 'Z')) {
    const auto traditional = Find(entries_, "=" + lower);
    if (
      traditional.size() > 2 && traditional[1] == ' '
      && (traditional[0] == 'A' || (traditional[0] == 'N' && mode == Mode::NAME) || (traditional[0] == 'U' && (mode == Mode::NAME || !Contains(lower))))
      && IsTraditionalReading(traditional.substr(2))
    ) {
      return std::string(traditional.substr(2));
    }
  }
  if (phrase) {
    return {};
  }
  if (word == casing && IsSurelyForeign(lower)) {
    if (auto foreign = ReadForeign(lower); !foreign.empty()) {
      return foreign;
    }
  }
  std::vector<Sound> sounds;
  if (mode == Mode::NAME || word != casing || !FromDictionary(entries_, lower, sounds)) {
    if (word == casing) {
      if (auto foreign = ReadForeign(lower); !foreign.empty()) {
        return foreign;
      }
    }
    // Non-ASCII spelling takes precedence over a coincidental English compound.
    const bool name_origin = mode == Mode::NAME || word != casing;
    const bool english_origin = IsEnglishByOrigin(entries_, lower, casing);
    bool sure = false;
    if (!name_origin && FromStructure(entries_, lower, sounds, sure) && (sure || english_origin)) {
      KeepOnePrimary(sounds);
      return Russian(entries_, sounds, lower);
    }
    if (!Find(entries_, "@[a]").empty() && (name_origin || !english_origin)) {
      return ReadName(entries_, lower, accents);
    }
    if (!SpellByRules(entries_, windows_, stress_ending_, lower, sounds) || sounds.empty()) {
      return !Find(entries_, "@[a]").empty() ? ReadName(entries_, lower, accents) : std::string();
    }
  }
  KeepOnePrimary(sounds);
  return Russian(entries_, sounds, lower);
}

std::string EnglishReader::Transcribe(std::string_view word) const {
  std::string lower;
  std::vector<Sound> sounds;
  if (!Lower(word, lower)) {
    return {};
  }
  if (!FromDictionary(entries_, lower, sounds) && !SpellByRules(entries_, windows_, stress_ending_, lower, sounds)) {
    return {};
  }
  KeepOnePrimary(sounds);
  std::string result;
  for (const auto& sound : sounds) {
    if (!result.empty()) {
      result += ' ';
    }
    result += kPhoneNames[static_cast<size_t>(sound.phone)];
    if (IsVowel(sound.phone)) {
      result += static_cast<char>('0' + sound.stress);
    }
  }
  return result;
}

bool EnglishReader::Contains(std::string_view word) const {
  std::string lower;
  std::vector<Sound> sounds;
  return Lower(word, lower) && FromDictionary(entries_, lower, sounds);
}

} // namespace giga_edge::vits
