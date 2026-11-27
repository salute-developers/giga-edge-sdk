#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace giga_edge::vits {

enum class GrammaticalCase { NOMINATIVE, GENITIVE, DATIVE, ACCUSATIVE, INSTRUMENTAL, PREPOSITIONAL };
inline constexpr int kCaseCount = 6;
enum class Gender { MASCULINE, FEMININE, NEUTER };

// The form of a counted noun: 1 час, 2 часа, 5 часов.
enum class CountForm { ONE, FEW, MANY };
CountForm CountFormOf(uint64_t value);

// Russian numerals in words. A '+' stands before the stressed vowel of a word ("дв+адцать"); words of one syllable
// and words stressed on 'ё' have no mark, and a 'ё' stays as written next to a '+' too ("трёхс+от"). The accusative
// is the inanimate one. Values above kMaxNumeral throw std::out_of_range.
inline constexpr uint64_t kMaxNumeral = 999'999'999'999'999ULL;

// 22, genitive -> "двадцат+и двух"; the gender shows in 1 and 2 ("одн+а", "две").
std::string Cardinal(uint64_t value, GrammaticalCase grammatical_case, Gender gender);

// 2024, prepositional -> "две т+ысячи дв+адцать четвёртом"; plural: "п+ервые".
std::string Ordinal(uint64_t value, GrammaticalCase grammatical_case, Gender gender, bool plural = false);

// 15 and "6" -> "пятн+адцать ц+елых шесть дес+ятых". The fraction is one to kMaxFractionDigits decimal digits as
// they are written ("06" is six hundredths); anything else throws std::invalid_argument.
inline constexpr int kMaxFractionDigits = 3;
std::string Decimal(uint64_t whole, std::string_view fraction_digits, GrammaticalCase grammatical_case);

// The first part of a compound word, without stress marks: 3 -> "трёх" ("трёхкомнатный"), 50 -> "пятидесяти",
// 1 -> "одно", 100 -> "сто". Values outside 1..999 throw std::out_of_range.
std::string CompoundPrefix(uint64_t value);

} // namespace giga_edge::vits
