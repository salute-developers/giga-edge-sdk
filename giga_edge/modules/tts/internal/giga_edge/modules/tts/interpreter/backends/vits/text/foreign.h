#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace giga_edge::vits {

inline constexpr size_t kMaxLatinLetters = 40;

// A word in Latin letters that is not English by sure signs, read as it is written, in Russian letters with one '+'
// before the stressed vowel, except Slavic words, whose stress is assigned by the downstream Russian stage:
//   German ("sch" outside English prefixes and stems, "-tz", "-stein", "-mann", ...): "rammstein" -> "раммшт+айн", st/sp of a stem -> "шт/шп", ei -> "ай";
//   Chinese ("xi-", "xu-", "qi-", "iao", "iang", "uang" in a word of pinyin syllables): "xiaomi" -> "сяом+и", x -> "с", q -> "ц", zh -> "чж";
//   Russian and other Slavic ("kh", "zh", "shch", "yu", "-ov", "-sky" after a consonant, "-ei", ...): "rakhim" -> "рахим";
//   open syllables of Japanese, Italian, Spanish (consonant-vowel pairs, the last letter a vowel other than 'e', no
//   English letter pairs): "miyagi" -> "ми+яги", "ferrari" -> "ферр+ари".
// `word` is lower-case Latin letters (an apostrophe makes it English); an empty result means "not sure, read it as
// English".
std::string ReadForeign(std::string_view word);

// Whether the word has signs of a Russian name strong enough to read it so even when the English dictionary has it
// ("smirnov", "dostoyevsky", "khrushchev", "zhanna"): zh, shch, kh at the start or between vowels, -ov, -ev, -ova,
// -eva, -sky, -skiy, -skaya after a consonant.
bool IsSurelyForeign(std::string_view word);

} // namespace giga_edge::vits
