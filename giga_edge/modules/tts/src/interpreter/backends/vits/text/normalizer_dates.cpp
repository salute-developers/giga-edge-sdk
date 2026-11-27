// The text normalizer: dates and times of day.
#include "giga_edge/modules/tts/interpreter/backends/vits/text/normalization.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace giga_edge::vits::normalization {

bool Normalization::IsDotTime(size_t index) const {
  constexpr std::string_view time_prepositions[] = {"в", "во", "к", "ко", "до", "с", "со", "после", "около", "от"};
  if (
    index == 0 || !tokens_[index].space_before || !IsWord(index - 1)
    || std::find(std::begin(time_prepositions), std::end(time_prepositions), tokens_[index - 1].lower)
      == std::end(time_prepositions)
  ) {
    return false;
  }
  const auto after = index + 3;
  if ((IsSymbol(after, ".") && IsNumber(after + 1) && Glued(after + 1)) || MatchUnit(after)) {
    return false;
  }
  if (!IsWord(after)) {
    return true;
  }
  // "в 9.30 утра", but "до 1.25 узла": a noun in the genitive after a fraction.
  if (IsDayPart(after)) {
    return true;
  }
  const auto readings = NounAfter(after, false).readings;
  return std::none_of(readings.begin(), readings.end(), [](const NounReading& reading) {
    return !reading.plural && reading.grammatical_case == kGen;
  });
}

std::optional<size_t> Normalization::TryTime(size_t index) {
  const auto colon = IsSymbol(index + 1, ":");
  // "в 09.05", "к 09.05" is a time: a date does not follow "в" or "к"; "с 09.05 по 12.05" is a date.
  const auto time_word = index > 0 && IsWord(index - 1)
    && (tokens_[index - 1].lower == "в" || tokens_[index - 1].lower == "во" || tokens_[index - 1].lower == "к");
  if (
    tokens_[index].text.size() > 2 || !(colon || IsSymbol(index + 1, ".")) || !Glued(index + 1) || !IsNumber(index + 2)
    || !Glued(index + 2) || tokens_[index + 2].text.size() != 2
    || (!colon && (!IsDotTime(index) || (IsShortDate(index) && !time_word)))
  ) {
    return std::nullopt;
  }
  constexpr uint64_t max_hour = 24;
  constexpr uint64_t max_minute = 59;
  const auto hours = ValueOf(tokens_[index].text);
  const auto minutes = ValueOf(tokens_[index + 2].text);
  if (minutes > max_minute) {
    return std::nullopt;
  }
  // "90:00" on a timer: minutes and seconds.
  const auto* minute_noun = NounByLemma("минута");
  const auto* second_noun = NounByLemma("секунда");
  if (hours > max_hour && colon && minute_noun != nullptr && second_noun != nullptr && !IsSymbol(index + 3, ":")) {
    Words words;
    const auto governed = GovernedBefore(index);
    for (const auto& [value, noun] : {std::pair{hours, minute_noun}, std::pair{minutes, second_noun}}) {
      if (value == 0 && noun == second_noun) {
        continue;
      }
      NumberContext context;
      context.governed = governed;
      context.noun = AnyCase(noun->gender, {false, true});
      const auto reading = Agree(value, context);
      words.Add(ReadNumber(value, reading));
      words.Add(CountedNoun(*noun, value, reading));
    }
    Add(words.Text(), PieceKind::WORD, tokens_[index].space_before);
    return index + 3;
  }
  std::optional<uint64_t> seconds;
  if (
    colon && IsSymbol(index + 3, ":") && Glued(index + 3) && IsNumber(index + 4) && Glued(index + 4)
    && tokens_[index + 4].text.size() == 2 && ValueOf(tokens_[index + 4].text) <= max_minute
  ) {
    seconds = ValueOf(tokens_[index + 4].text);
  }
  // "25:01:00" is a duration; hours of a day are up to 24.
  if (hours > max_hour && !seconds) {
    return std::nullopt;
  }
  const auto governed = GovernedBefore(index);
  const auto grammatical_case = governed.empty() ? kNom : governed.front();
  Words words;
  const auto* hour_noun = NounByLemma("час");
  if (seconds && hour_noun != nullptr && minute_noun != nullptr && second_noun != nullptr) {
    // A duration: "два час+а пятн+адцать мин+ут тр+идцать сек+унд"; zero parts are not read ("00:05:00").
    const auto all_zero = hours == 0 && minutes == 0 && *seconds == 0;
    for (const auto& [value, noun] :
         {std::pair{hours, hour_noun}, std::pair{minutes, minute_noun}, std::pair{*seconds, second_noun}}) {
      if (value == 0 && !(all_zero && noun == second_noun)) {
        continue;
      }
      NumberContext context;
      context.governed = governed;
      context.noun = AnyCase(noun->gender, {false, true});
      const auto reading = Agree(value, context);
      words.Add(ReadNumber(value, reading));
      words.Add(CountedNoun(*noun, value, reading));
    }
  } else {
    // "10:00–22:00": "с десят+и ноль ноль до двадцат+и двух ноль ноль".
    const auto dash = index + 3;
    const auto range = !seconds && (IsSymbol(dash, "-") || IsSymbol(dash, "–") || IsSymbol(dash, "—"))
      && IsNumber(dash + 1) && tokens_[dash + 1].text.size() <= 2 && IsSymbol(dash + 2, ":") && IsNumber(dash + 3)
      && tokens_[dash + 3].text.size() == 2 && ValueOf(tokens_[dash + 1].text) <= max_hour
      && ValueOf(tokens_[dash + 3].text) <= max_minute;
    if (range) {
      const auto bound_case = governed.empty() ? kGen : grammatical_case;
      words.Add(governed.empty() ? "с" : "");
      words.Add(ReadClock(hours, {minutes}, bound_case));
      words.Add(governed.empty() ? "до" : "-");
      words.Add(ReadClock(ValueOf(tokens_[dash + 1].text), {ValueOf(tokens_[dash + 3].text)}, bound_case));
      Add(words.Text(), PieceKind::WORD, tokens_[index].space_before);
      return dash + 4;
    }
    words.Add(ReadClock(hours, {minutes, seconds}, grammatical_case));
  }
  Add(words.Text(), PieceKind::WORD, tokens_[index].space_before);
  return index + (seconds ? 5 : 3);
}

std::string Normalization::ReadClock(
  uint64_t hours, std::initializer_list<std::optional<uint64_t>> parts, GrammaticalCase grammatical_case
) {
  Words words;
  words.Add(Cardinal(hours, grammatical_case, Gender::MASCULINE));
  for (const auto value : parts) {
    if (!value) {
      continue;
    }
    constexpr uint64_t ten = 10;
    if (*value < ten) {
      words.Add("ноль");
    }
    words.Add(*value == 0 ? "ноль" : Cardinal(*value, grammatical_case, Gender::MASCULINE));
  }
  return words.Text();
}

std::vector<GrammaticalCase> Normalization::DateCases(size_t index, size_t after) const {
  // "понед+ельник, шестн+адцатого ноября".
  if (
    index >= 2 && IsSymbol(index - 1, ",") && IsWord(index - 2)
    && lexicon_.IsServiceWord("weekday", tokens_[index - 2].lower)
  ) {
    return {kGen};
  }
  // "сегодня уже 5 мая": through the words that do not govern.
  while (index > 1 && IsWord(index - 1) && lexicon_.IsServiceWord("transparent", tokens_[index - 1].lower)) {
    --index;
  }
  // "1 сентября — День знаний": a date alone at the start of a sentence is a nominative; "24 ноября будет солнечно",
  // "Завтра, 25 ноября, …" are when, a genitive.
  if (index == 0 || !IsWord(index - 1)) {
    const auto start = index == 0 || IsSentenceEnd(index - 1) || IsSymbol(index - 1, ":") || IsSymbol(index - 1, "(");
    const auto alone = after >= tokens_.size() || IsSentenceEnd(after) || IsSymbol(after, "—") || IsSymbol(after, "–")
      || IsSymbol(after, "-") || IsSymbol(after, ":") || IsSymbol(after, ";");
    return {start && alone ? kNom : kGen};
  }
  const auto& word = tokens_[index - 1].lower;
  return {lexicon_.IsServiceWord("nominative_date", word) ? kNom : kGen};
}

size_t Normalization::DateEnd(size_t month) const {
  auto end = month + 1;
  if (IsSymbol(end, ".") && Glued(end)) {
    ++end;
  }
  if (IsNumber(end) && tokens_[end].text.size() == 4) {
    ++end;
    if (IsWord(end) && tokens_[end].lower == "года") {
      ++end;
    } else if (IsWord(end) && tokens_[end].text == "г" && IsSymbol(end + 1, ".")) {
      end += 2;
    }
  }
  return end;
}

std::string Normalization::ReadDay(
  uint64_t day, size_t index, size_t after, const std::vector<GrammaticalCase>& governed, const std::string& suffix
) const {
  NumberContext context;
  context.governed = governed.empty() ? DateCases(index, after) : governed;
  if (suffix.empty()) {
    return Ordinal(day, context.governed.front(), Gender::NEUTER);
  }
  // "1-го января".
  context.noun = AnyCase(Gender::NEUTER, {false});
  context.ordinal_use = OrdinalUse::ALWAYS;
  context.suffix = suffix;
  return ReadNumber(day, Agree(day, context));
}

size_t Normalization::AddYear(Words& words, size_t index, bool always_noun) const {
  if (
    IsNumber(index) && tokens_[index].text.size() == 4 && !IsSymbol(index + 1, ":")
    && !((IsSymbol(index + 1, ".") || IsSymbol(index + 1, ",")) && IsNumber(index + 2) && Glued(index + 2))
  ) {
    words.Add(Ordinal(ValueOf(tokens_[index].text), kGen, Gender::MASCULINE));
    ++index;
    if (IsWord(index) && tokens_[index].text == "г" && IsSymbol(index + 1, ".") && Glued(index + 1)) {
      words.Add("г+ода");
      return SkipAbbreviationDot(index + 1);
    }
    if (IsWord(index) && tokens_[index].lower == "года") {
      words.Add(tokens_[index].lower);
      return index + 1;
    }
    if (always_noun) {
      words.Add("г+ода");
    }
  }
  return index;
}

bool Normalization::IsShortDate(size_t index) const {
  constexpr uint64_t max_day = 31;
  constexpr uint64_t month_count = 12;
  if (
    !IsSymbol(index + 1, ".") || !IsNumber(index + 2) || tokens_[index].text.size() != 2
    || tokens_[index + 2].text.size() != 2
    || (tokens_[index].text.front() != '0' && tokens_[index + 2].text.front() != '0')
  ) {
    return false;
  }
  const auto day = ValueOf(tokens_[index].text);
  const auto month = ValueOf(tokens_[index + 2].text);
  return day >= 1 && day <= max_day && month >= 1 && month <= month_count;
}

std::optional<size_t> Normalization::TryIsoDate(size_t index) {
  if (
    tokens_[index].text.size() != 4 || !IsSymbol(index + 1, "-") || !Glued(index + 1) || !IsNumber(index + 2)
    || !Glued(index + 2) || tokens_[index + 2].text.size() != 2 || !IsSymbol(index + 3, "-") || !Glued(index + 3)
    || !IsNumber(index + 4) || !Glued(index + 4) || tokens_[index + 4].text.size() != 2
    || (IsSymbol(index + 5, "-") && Glued(index + 5))
  ) {
    return std::nullopt;
  }
  constexpr uint64_t max_day = 31;
  const auto month = ValueOf(tokens_[index + 2].text);
  const auto day = ValueOf(tokens_[index + 4].text);
  if (day < 1 || day > max_day || month < 1 || month > kMonths.size()) {
    return std::nullopt;
  }
  Words words;
  words.Add(ReadDay(day, index, index + 5, GovernedBefore(index), {}));
  words.Add(kMonths[month - 1]);
  words.Add(Ordinal(ValueOf(tokens_[index].text), kGen, Gender::MASCULINE));
  words.Add("г+ода");
  Add(words.Text(), PieceKind::WORD, tokens_[index].space_before);
  return index + 5;
}

std::optional<size_t> Normalization::TryNumericDate(size_t index) {
  if (
    !IsSymbol(index + 1, ".") || !Glued(index + 1) || !IsNumber(index + 2) || !Glued(index + 2)
    || tokens_[index].text.size() > 2 || tokens_[index + 2].text.size() > 2
  ) {
    return std::nullopt;
  }
  const auto year_digits = IsNumber(index + 4) ? tokens_[index + 4].text.size() : 0;
  const auto with_year = IsSymbol(index + 3, ".") && Glued(index + 3) && Glued(index + 4)
    && (year_digits == 4 || year_digits == 2) && !(IsSymbol(index + 5, ".") && IsNumber(index + 6) && Glued(index + 6));
  // "05.06" without a year: two digits each, a leading zero, no noun or unit after it, no more dots.
  const auto short_date = !with_year && tokens_[index].text.size() == 2 && tokens_[index + 2].text.size() == 2
    && (tokens_[index].text.front() == '0' || tokens_[index + 2].text.front() == '0')
    && !(IsSymbol(index + 3, ".") && IsNumber(index + 4)) && !MatchUnit(index + 3)
    && (!IsWord(index + 3) || NounAfter(index + 3, false).readings.empty());
  if (!with_year && !short_date) {
    return std::nullopt;
  }
  constexpr uint64_t max_day = 31;
  const auto day = ValueOf(tokens_[index].text);
  const auto month = ValueOf(tokens_[index + 2].text);
  if (day < 1 || day > max_day || month < 1 || month > kMonths.size()) {
    return std::nullopt;
  }
  Words words;
  // "датирован 09.05.1963": a word of the tables governs a date; a guess by the ending does not ("подписали 4 февр.").
  auto governed = GovernedBefore(index);
  if (governed.empty()) {
    governed = TableGovernment(index);
  }
  words.Add(ReadDay(day, index, with_year ? DateEnd(index + 3) : index + 3, governed, {}));
  words.Add(kMonths[month - 1]);
  auto end = index + 3;
  if (with_year && year_digits == 2) {
    // "12.04.61": "шестьдес+ят п+ервого г+ода".
    words.Add(Ordinal(ValueOf(tokens_[index + 4].text), kGen, Gender::MASCULINE));
    words.Add("г+ода");
    end = index + 5;
  } else if (with_year) {
    end = AddYear(words, index + 4, true);
  }
  Add(words.Text(), PieceKind::WORD, tokens_[index].space_before);
  return end;
}

std::optional<size_t> Normalization::TryDayRange(size_t index, uint64_t from, const Amount& second) {
  constexpr uint64_t max_day = 31;
  const auto to = ValueOf(second.digits);
  const auto month = NounAfter(second.end, false);
  if (from < 1 || to > max_day || month.month < 0 || month.position == 0) {
    return std::nullopt;
  }
  const auto governed = GovernedBefore(index);
  Words words;
  if (governed.empty()) {
    words.Add("с");
    words.Add(Ordinal(from, kGen, Gender::NEUTER));
    words.Add("по");
    words.Add(Ordinal(to, Case::ACCUSATIVE, Gender::NEUTER));
  } else {
    words.Add(Ordinal(from, governed.front(), Gender::NEUTER));
    words.Add("-");
    words.Add(Ordinal(to, governed.front(), Gender::NEUTER));
  }
  auto end = month.position + 1;
  if (month.month_abbreviation) {
    words.Add(kMonths[static_cast<size_t>(month.month)]);
    end = SkipAbbreviationDot(end);
  } else {
    words.Add(tokens_[month.position].lower);
  }
  end = AddYear(words, end, false);
  Add(words.Text(), PieceKind::WORD, tokens_[index].space_before);
  return end;
}

std::optional<size_t> Normalization::TryYearPair(size_t index, uint64_t from, const Amount& second) {
  constexpr uint64_t min_year = 1000;
  const auto* year = NounByLemma("год");
  const auto to = ValueOf(second.digits);
  if (year == nullptr || to < min_year || !second.units.empty() || second.sign != 0) {
    return std::nullopt;
  }
  const auto governed = GovernedBefore(index);
  std::vector<GrammaticalCase> cases;
  if (second.year_abbreviation) {
    if (!second.year_plural) {
      return std::nullopt;
    }
    // Generated: the genitive alone ("... годов"), the prepositional after "в" ("в ... годах").
    cases = {kGen, Case::PREPOSITIONAL, Case::DATIVE, Case::INSTRUMENTAL, Case::ACCUSATIVE};
  } else {
    const auto noun = NounAfter(second.end, false);
    if (noun.noun != year) {
      return std::nullopt;
    }
    for (const auto& reading : noun.readings) {
      if (reading.plural) {
        cases.push_back(reading.grammatical_case);
      }
    }
    if (cases.empty()) {
      return std::nullopt;
    }
  }
  auto grammatical_case = cases.front();
  for (const auto preferred : {Case::PREPOSITIONAL, Case::DATIVE, kGen, Case::INSTRUMENTAL, Case::ACCUSATIVE, kNom}) {
    if (
      std::find(governed.begin(), governed.end(), preferred) != governed.end()
      && std::find(cases.begin(), cases.end(), preferred) != cases.end()
    ) {
      grammatical_case = preferred;
      break;
    }
  }
  Words words;
  words.Add(Ordinal(from, grammatical_case, Gender::MASCULINE));
  words.Add("-");
  words.Add(Ordinal(to, grammatical_case, Gender::MASCULINE));
  if (second.year_abbreviation) {
    words.Add(year->plural[static_cast<int>(grammatical_case)]);
  }
  Add(words.Text(), PieceKind::WORD, tokens_[index].space_before);
  return second.end;
}

} // namespace giga_edge::vits::normalization
