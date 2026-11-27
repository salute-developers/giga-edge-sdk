// The text normalizer: numbers, their agreement and government, ranges, sequences, telephones, Roman numerals.
#include "giga_edge/modules/tts/interpreter/backends/vits/text/normalization.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace giga_edge::vits::normalization {

std::optional<Amount> Normalization::ParseAmount(size_t index) const {
  Amount amount;
  amount.begin = index;
  const Unit* currency_before = nullptr;
  if (index < tokens_.size() && tokens_[index].type == TokenType::SYMBOL) {
    const auto* unit = lexicon_.FindUnit(tokens_[index].text);
    if (unit != nullptr && unit->noun->subunit != nullptr && IsNumber(index + 1)) {
      currency_before = unit;
      ++index;
    } else if (IsSign(index)) {
      amount.sign = tokens_[index].text == "+" ? 1 : -1;
      ++index;
    }
  }
  if (!IsNumber(index)) {
    return std::nullopt;
  }
  amount.digits = tokens_[index].text;
  ++index;
  // Groups of three digits: "1 234 567".
  if (amount.digits.size() <= 3 && amount.digits.front() != '0') {
    while (IsNumber(index) && tokens_[index].space_before && tokens_[index].text.size() == 3) {
      amount.digits += tokens_[index].text;
      ++index;
    }
  }
  if ((IsSymbol(index, ".") || IsSymbol(index, ",")) && Glued(index) && IsNumber(index + 1) && Glued(index + 1)) {
    amount.fraction = tokens_[index + 1].text;
    index += 2;
  }
  const auto one_and_a_half = amount.digits == "1" && amount.fraction == "5";
  if (
    (amount.fraction.empty() || one_and_a_half) && IsSymbol(index, "-") && Glued(index) && IsWord(index + 1)
    && Glued(index + 1) && static_cast<unsigned char>(tokens_[index + 1].text.front()) >= 0x80
  ) {
    const auto& word = tokens_[index + 1].lower;
    constexpr size_t max_suffix_bytes = 6; // three Cyrillic letters
    (word.size() <= max_suffix_bytes ? amount.suffix : amount.compound) = word;
    index += 2;
  }
  if (!amount.suffix.empty()) {
    // "47-й км".
    if (const auto unit = MatchUnit(index)) {
      amount.units.push_back(unit->first);
      index = unit->second;
    }
  } else if (amount.compound.empty()) {
    const auto year = amount.sign == 0 && amount.fraction.empty() && amount.digits.size() == 4;
    if (
      year && currency_before == nullptr && !IsMassWord(amount.begin) && IsWord(index)
      && (tokens_[index].text == "г" || tokens_[index].text == "гг") && IsSymbol(index + 1, ".") && Glued(index + 1)
      && NounByLemma("год") != nullptr
    ) {
      amount.year_abbreviation = true;
      amount.year_plural = tokens_[index].text == "гг";
      index = SkipAbbreviationDot(index + 1);
    } else {
      while (amount.units.size() < (currency_before == nullptr ? 2U : 1U)
             && (amount.units.empty() || IsMultiplier(*amount.units.front()->noun))) {
        const auto unit = MatchUnit(index);
        // "4K", "8K" are a resolution, "2700K", "999K" a temperature.
        constexpr uint64_t resolutions[] = {1, 2, 4, 5, 6, 8, 10, 12, 16};
        if (!unit || (IsWord(index) && tokens_[index].text == "K" && amount.digits.size() <= kMaxNumeralDigits
                      && std::find(std::begin(resolutions), std::end(resolutions), ValueOf(amount.digits))
                        != std::end(resolutions))) {
          break;
        }
        amount.units.push_back(unit->first);
        index = unit->second;
      }
      if (currency_before != nullptr) {
        amount.units.push_back(currency_before);
      }
    }
  }
  amount.end = index;
  return amount;
}

std::vector<GrammaticalCase> Normalization::GovernedBefore(size_t index) const {
  std::vector<size_t> chain;
  std::vector<GrammaticalCase> cases;
  while (true) {
    if (const auto known = governed_.find(index); known != governed_.end()) {
      cases = known->second;
      break;
    }
    chain.push_back(index);
    const auto previous = GovernedStep(index, cases);
    if (!previous) {
      break;
    }
    index = *previous;
  }
  for (const auto link : chain) {
    governed_[link] = cases;
  }
  return cases;
}

std::optional<size_t> Normalization::GovernedStep(size_t index, std::vector<GrammaticalCase>& cases) const {
  cases.clear();
  if (index > 0 && IsSymbol(index - 1, "~")) {
    cases = {kGen};
    return std::nullopt;
  }
  // "3+5=8": "равн+о восьм+и"; "3 > 1": "б+ольше одног+о"; "4.5/5": "из пят+и".
  if (index > 0 && IsSymbol(index - 1, "=")) {
    cases = {Case::DATIVE};
    return std::nullopt;
  }
  if (
    index > 0 && (IsSymbol(index - 1, ">") || IsSymbol(index - 1, "<") || (IsSymbol(index - 1, "/") && Glued(index)))
  ) {
    cases = {kGen};
    return std::nullopt;
  }
  while (index > 0 && tokens_[index].space_before && IsWord(index - 1)
         && lexicon_.IsServiceWord("transparent", tokens_[index - 1].lower)) {
    --index;
  }
  if (index == 0 || !tokens_[index].space_before) {
    return std::nullopt;
  }
  auto unit = index - 1;
  if (IsSymbol(unit, ".") && !tokens_[unit].space_before && unit > 0) {
    --unit;
  }
  if (
    unit > 0 && IsNumber(unit - 1) && tokens_[unit].type != TokenType::NUMBER
    && lexicon_.FindUnit(tokens_[unit].text) != nullptr && lexicon_.GovernedCases(tokens_[unit].lower).empty()
  ) {
    return NumberStart(unit - 1);
  }
  if (!IsWord(index - 1)) {
    return std::nullopt;
  }
  // "c 5 до 7" with a Latin "c".
  const auto& written = tokens_[index - 1].lower;
  const auto word = written.size() == 1 && std::string_view("acoyk").find(written) != std::string_view::npos
    ? RussianLetters(written, true)
    : written;
  // "на сумму 854 руб.": a phrase of two words governs before its last word.
  if (index >= 2 && IsWord(index - 2)) {
    cases = lexicon_.GovernedCases(tokens_[index - 2].lower + " " + word);
  }
  if (cases.empty()) {
    cases = lexicon_.GovernedCases(word);
  }
  if (cases.empty() && (word == "и" || word == "или") && index >= 2) {
    // "между 2 и 3", "для 3 детей и 2 взрослых": the number before, maybe with its noun.
    for (size_t back = 2; back <= 4 && back <= index; ++back) {
      if (IsNumber(index - back)) {
        return NumberStart(index - back);
      }
      if (!IsWord(index - back)) {
        break;
      }
    }
  }
  return std::nullopt;
}

NounContext Normalization::NounAfter(size_t index, bool look_through, bool plural_context) const {
  NounContext context;
  if (!IsWord(index)) {
    return context;
  }
  const auto& word = tokens_[index].lower;
  for (size_t month = 0; month < kMonths.size(); ++month) {
    const auto abbreviation = IsSymbol(index + 1, ".") && Glued(index + 1)
      && std::find(kMonthAbbreviations[month].begin(), kMonthAbbreviations[month].end(), word)
        != kMonthAbbreviations[month].end();
    if (LookupKey(kMonths[month]) == LookupKey(word) || abbreviation) {
      context.month = static_cast<int>(month);
      context.position = index;
      context.month_abbreviation = abbreviation;
      return context;
    }
  }
  if (look_through && (word == "и" || word == "или" || word == "по" || word == "до")) {
    const auto next = ParseAmount(index + 1);
    if (!next) {
      return context;
    }
    return SharedNoun(*next);
  }
  // "1 новую умную лампу", "в 3 соседних комнатах", "2 большие лампы": the noun after the adjectives.
  auto noun_index = index;
  constexpr size_t max_adjectives = 3;
  for (size_t skipped = 0; skipped < max_adjectives && IsWord(noun_index + 1); ++skipped) {
    const auto& candidate = tokens_[noun_index].lower;
    const auto ends = [&candidate](std::string_view ending) {
      return candidate.size() > ending.size() + 4
        && candidate.compare(candidate.size() - ending.size(), ending.size(), ending) == 0;
    };
    const auto plural_adjective = plural_context && (ends("ие") || ends("ые"));
    // "по 1 чайной ложке", "в 1 большом доме": -ой, -ей, -ом, -ем only before a noun.
    const auto& next = tokens_[noun_index + 1].lower;
    const auto before_noun = lexicon_.FindNoun(next) != nullptr || !Guess(next).empty();
    const auto weak_adjective = before_noun && (ends("ой") || ends("ей") || ends("ом") || ends("ем"));
    // The dictionary of words tells a noun from an adjective: "742 589 бюллетеней комиссия"; a word of both is the noun
    // when no noun follows: "3 часовых перед".
    const auto forms = word_info_ ? word_info_(candidate) : std::vector<WordForm>();
    const auto has = [&forms](PartOfSpeech part) {
      return std::any_of(forms.begin(), forms.end(), [part](const WordForm& form) {
        return form.part == part;
      });
    };
    const auto dictionary_noun = has(PartOfSpeech::NOUN) && (!before_noun || !has(PartOfSpeech::ADJECTIVE));
    // An adjective in an oblique case agrees with the word after it: not "учащимся техникумов", "ведущими научных".
    const auto oblique = ObliqueAdjectiveCases(candidate);
    const auto next_cases = [&]() {
      std::vector<GrammaticalCase> cases;
      if (LooksLikeAdjective(next)) {
        return AdjectiveCases(next);
      }
      const auto* match = lexicon_.FindNoun(next);
      for (const auto& reading : match != nullptr ? match->readings : Guess(next)) {
        cases.push_back(reading.grammatical_case);
      }
      return cases;
    };
    const auto disagrees = !oblique.empty() && [&]() {
      const auto cases = next_cases();
      return !cases.empty() && std::none_of(oblique.begin(), oblique.end(), [&cases](GrammaticalCase grammatical_case) {
        return std::find(cases.begin(), cases.end(), grammatical_case) != cases.end();
      });
    }();
    if (
      lexicon_.FindNoun(candidate) != nullptr || dictionary_noun || disagrees
      || !(LooksLikeAdjective(candidate) || plural_adjective || weak_adjective)
    ) {
      break;
    }
    ++noun_index;
    context.adjective = true;
  }
  const auto& noun_word = tokens_[noun_index].lower;
  if (const auto* match = lexicon_.FindNoun(noun_word)) {
    context.readings = match->readings;
    context.ordinal_use = match->noun->ordinal_use;
    context.noun = match->noun;
  } else if (
    const auto forms = word_info_ && !lexicon_.IsNotNoun(noun_word) ? word_info_(noun_word) : std::vector<WordForm>();
    !forms.empty()
  ) {
    // The dictionary of words knows the word: its noun readings or none.
    for (const auto& form : forms) {
      if (form.part == PartOfSpeech::NOUN) {
        context.readings.push_back(form.reading);
        context.plural_only = context.plural_only || form.plural_only;
      }
    }
    context.known = true;
  } else {
    context.readings = Guess(noun_word);
  }
  // An adjective in an oblique case standing for its noun: "назначили 137 954 учащимся", "с 9 ведущими".
  if (context.readings.empty() && noun_index == index) {
    context.readings = ObliqueAdjectiveReadings(noun_word);
  }
  // No noun after an adjective of persons (words.tsv): it stands for the noun when a word governs the number
  // ("осмотрел 21 больного"); a subject stays as it is ("остались 3 рабочих"), so does "купил 2 больших".
  const auto& adjective = tokens_[context.adjective ? noun_index - 1 : noun_index].lower;
  if (
    context.readings.empty() && (context.adjective || noun_index == index) && index > 0 && IsNumber(index - 1)
    && lexicon_.IsServiceWord("person", adjective)
  ) {
    const auto number = NumberStart(index - 1);
    if (!GovernedBefore(number).empty() || !WordGovernment(number).empty()) {
      context.readings = SubstantiveReadings(adjective);
    }
  }
  context.position = noun_index;
  return context;
}

NounContext Normalization::SharedNoun(const Amount& next) const {
  NounContext shared;
  if (!next.units.empty()) {
    shared.readings = {{next.units.front()->noun->gender, false, kNom}};
    shared.ordinal_use = next.units.front()->noun->ordinal_use;
  } else {
    shared = NounAfter(next.end, false);
    shared.day_part = shared.position != 0 && IsDayPart(shared.position);
  }
  const auto singular = std::any_of(shared.readings.begin(), shared.readings.end(), [](const NounReading& reading) {
    return !reading.plural;
  });
  if (!singular) {
    shared.ordinal_use = OrdinalUse::NONE;
  }
  // "с 11 по 19 километр": the noun agrees with the next number only as an ordinal, so does this one.
  NumberContext own;
  own.noun = shared.readings;
  const auto ordinal = next.units.empty() && next.fraction.empty() && !own.noun.empty()
    && Agree(ValueOf(next.digits), own).ordinal;
  std::vector<NounReading> widened;
  for (const auto& reading : shared.readings) {
    for (const bool plural : {false, true}) {
      if (ordinal && plural) {
        continue;
      }
      for (int case_index = 0; case_index < kCaseCount; ++case_index) {
        widened.push_back({reading.gender, plural, static_cast<Case>(case_index)});
      }
    }
  }
  // "принял 21 и 31 пациента": the animate accusative of the noun goes to the numbers before it, in place of the
  // accusative of the inanimate.
  for (const auto& reading : shared.readings) {
    if (!reading.animate) {
      continue;
    }
    for (auto& wide : widened) {
      if (wide.grammatical_case == Case::ACCUSATIVE && wide.gender == reading.gender && wide.plural == reading.plural) {
        wide.animate = true;
      }
    }
  }
  shared.readings = std::move(widened);
  shared.position = 0;
  return shared;
}

std::optional<size_t> Normalization::TryPhone(size_t index) {
  const auto plus = IsSymbol(index, "+") && IsNumber(index + 1) && Glued(index + 1);
  auto current = plus ? index + 1 : index;
  if (!IsNumber(current) || (index > 0 && !plus && !tokens_[index].space_before && IsNumber(index - 1))) {
    return std::nullopt;
  }
  constexpr size_t max_groups = 8;
  std::vector<size_t> groups = {current};
  bool dash = false;
  bool brackets = false;
  while (groups.size() < max_groups) {
    const auto next = groups.back() + 1;
    if (IsSymbol(next, "-") && Glued(next) && IsNumber(next + 1) && Glued(next + 1)) {
      dash = true;
      groups.push_back(next + 1);
    } else if (
      IsSymbol(next, "(") && IsNumber(next + 1) && Glued(next + 1) && IsSymbol(next + 2, ")") && Glued(next + 2)
      && IsNumber(next + 3)
    ) {
      brackets = true;
      groups.push_back(next + 1);
      groups.push_back(next + 3);
    } else if (IsNumber(next) && tokens_[next].space_before) {
      groups.push_back(next);
    } else {
      break;
    }
  }
  size_t digits = 0;
  bool thousands = !dash && !brackets && tokens_[groups.front()].text.size() <= 3;
  for (const auto group : groups) {
    digits += tokens_[group].text.size();
    thousands = thousands && (group == groups.front() || tokens_[group].text.size() == 3);
  }
  constexpr size_t min_digits = 10;
  constexpr size_t max_digits = 12;
  // "+7 495 123-45-67", but "+1 000 рублей" is a sum: a number with "+" has the digits of a telephone.
  constexpr size_t min_plus_digits = 7;
  const auto phone = (plus && groups.size() >= 2 && digits >= min_plus_digits && !thousands)
    || (groups.size() >= 3 && digits >= min_digits && digits <= max_digits && !thousands);
  if (!phone) {
    return std::nullopt;
  }
  Words words;
  if (plus) {
    words.Add("плюс");
  }
  for (const auto group : groups) {
    for (const auto part : PhoneParts(tokens_[group].text)) {
      if (!words.Text().empty() && words.Text() != "плюс") {
        words.Add(",");
      }
      words.Add(ReadGroup(part));
    }
  }
  Add(words.Text(), PieceKind::WORD, tokens_[index].space_before);
  return groups.back() + 1;
}

std::vector<std::string_view> Normalization::PhoneParts(std::string_view group) {
  constexpr size_t max_part = 3;
  std::vector<std::string_view> parts;
  while (group.size() > max_part) {
    const auto size = group.size() % 2 == 1 ? 3 : 2;
    parts.push_back(group.substr(0, size));
    group.remove_prefix(size);
  }
  parts.push_back(group);
  return parts;
}

std::optional<size_t> Normalization::TryNumber(size_t index) {
  const auto sign = tokens_[index].type == TokenType::SYMBOL && IsNumber(index + 1);
  const auto* currency = sign ? lexicon_.FindUnit(tokens_[index].text) : nullptr;
  const auto starts = IsNumber(index)
    || (sign && (Glued(index + 1) || (currency != nullptr && currency->noun->subunit != nullptr)));
  if (!starts) {
    return std::nullopt;
  }
  // "1. Нарежьте лук.", "2) Посолите": the number of an item of a list at the start of a line, of one or two digits,
  // in a list of several items; "2024. Это был сложный год." is read.
  if (
    IsListItem(index)
    && std::count_if(
         tokens_.begin(),
         tokens_.end(),
         [](const Token& token) {
           return token.line_start && token.type == TokenType::NUMBER;
         }
       )
      > 1
  ) {
    const auto items = std::count_if(tokens_.begin(), tokens_.end(), [this, &index](const Token& token) {
      const auto at = static_cast<size_t>(&token - tokens_.data());
      return at != index && IsListItem(at);
    });
    if (items > 0) {
      return index + 2;
    }
  }
  if (IsNumber(index)) {
    if (const auto end = TryReading(index)) {
      return end;
    }
  }
  if (const auto end = TryPhone(index)) {
    return end;
  }
  if (IsNumber(index) && IsVersionWord(index)) {
    if (const auto end = TryDotted(index)) {
      return end;
    }
  }
  if (IsNumber(index)) {
    for (const auto try_class :
         {&Normalization::TryIsoDate,
          &Normalization::TryTime,
          &Normalization::TryNumericDate,
          &Normalization::TryDotted,
          &Normalization::TrySequence,
          &Normalization::TrySlash,
          &Normalization::TryRange}) {
      if (const auto end = (this->*try_class)(index)) {
        return end;
      }
    }
  }
  const auto amount = ParseAmount(index);
  if (!amount) {
    return std::nullopt;
  }
  return EmitAmount(*amount);
}

std::optional<size_t> Normalization::TryDotted(size_t index) {
  Words words;
  words.Add(ReadGroup(tokens_[index].text));
  auto end = index + 1;
  int groups = 1;
  while (IsSymbol(end, ".") && Glued(end) && IsNumber(end + 1) && Glued(end + 1)) {
    words.Add("т+очка");
    words.Add(ReadGroup(tokens_[end + 1].text));
    end += 2;
    ++groups;
  }
  constexpr int min_groups = 3;
  if (groups < (IsVersionWord(index) ? 2 : min_groups)) {
    return std::nullopt;
  }
  Add(words.Text(), PieceKind::WORD, tokens_[index].space_before);
  return end;
}

std::optional<size_t> Normalization::TrySequence(size_t index) {
  std::vector<size_t> groups = {index};
  while (IsSymbol(groups.back() + 1, "-") && Glued(groups.back() + 1) && IsNumber(groups.back() + 2)
         && Glued(groups.back() + 2)) {
    groups.push_back(groups.back() + 2);
  }
  constexpr size_t min_dash_groups = 3;
  if (groups.size() < min_dash_groups) {
    groups = {index};
    while (IsNumber(groups.back() + 1) && tokens_[groups.back() + 1].space_before) {
      groups.push_back(groups.back() + 1);
    }
    const auto& first = tokens_[index].text;
    const auto thousands = first.size() <= 3 && first.front() != '0'
      && std::all_of(groups.begin() + 1, groups.end(), [this](size_t group) {
                             return tokens_[group].text.size() == 3;
                           });
    const auto& digits = tokens_[index].text;
    const auto decimal = (IsSymbol(index + 1, ".") || IsSymbol(index + 1, ",")) && Glued(index + 1)
      && IsNumber(index + 2) && Glued(index + 2);
    const auto digit_by_digit = groups.size() == 1 && !decimal
      && ((digits.size() > 1 && digits.front() == '0') || digits.size() > kMaxNumeralDigits);
    if (!(groups.size() > 1 && !thousands) && !digit_by_digit) {
      return std::nullopt;
    }
  }
  Words words;
  // Groups with a pause between them: a number is dictated group by group.
  for (const auto group : groups) {
    if (group != groups.front()) {
      words.Add(",");
    }
    words.Add(ReadGroup(tokens_[group].text));
  }
  Add(words.Text(), PieceKind::WORD, tokens_[index].space_before);
  return groups.back() + 1;
}

std::optional<size_t> Normalization::TryRange(size_t index) {
  const auto& first_digits = tokens_[index].text;
  const auto dash = index + 1;
  const auto is_dash = IsSymbol(dash, "-") || IsSymbol(dash, "–") || IsSymbol(dash, "—");
  if (
    first_digits.size() > kMaxNumeralDigits || (first_digits.size() > 1 && first_digits.front() == '0') || !is_dash
    || !Glued(dash) || !IsNumber(dash + 1) || !Glued(dash + 1)
  ) {
    return std::nullopt;
  }
  const auto second = ParseAmount(dash + 1);
  if (
    !second || !second->fraction.empty() || !second->suffix.empty() || !second->compound.empty()
    || second->digits.size() > kMaxNumeralDigits || (second->digits.size() > 1 && second->digits.front() == '0')
  ) {
    return std::nullopt;
  }
  const auto from = ValueOf(first_digits);
  const auto to = ValueOf(second->digits);
  if (from >= to) {
    return std::nullopt;
  }
  if (const auto end = TryYearPair(index, from, *second)) {
    return end;
  }
  if (second->year_abbreviation) {
    return std::nullopt;
  }
  const auto shared = SharedNoun(*second);
  if (shared.month >= 0) {
    return TryDayRange(index, from, *second);
  }
  NumberContext context;
  context.governed = GovernedBefore(index);
  context.noun = shared.readings;
  context.ordinal_use = shared.ordinal_use;
  // Without a preposition "от ... до ...", unless the noun is written for the bare range: "1-2 минуты".
  // A unit is read once after two numbers: "в растворе 54–68 мин".
  auto explicit_bounds = context.governed.empty() && second->units.empty();
  if (explicit_bounds) {
    const auto noun = NounAfter(second->end, false).readings;
    const NumeralReading genitive{false, kGen, Gender::MASCULINE, false};
    // Nothing counted: two numbers, "образцы 357–364", "цифры 47-92".
    explicit_bounds = std::any_of(noun.begin(), noun.end(), [&](const NounReading& reading) {
      return Fits(to, genitive, reading);
    });
  }
  if (explicit_bounds) {
    context.governed = {kGen};
  }
  Words words;
  if (explicit_bounds) {
    words.Add("от");
  }
  words.Add(ReadNumber(from, Agree(from, context)));
  words.Add(explicit_bounds ? "до" : "-");
  const auto end = AddAmount(words, *second, context.governed);
  Add(words.Text(), PieceKind::WORD, tokens_[index].space_before);
  return end;
}

std::optional<size_t> Normalization::TryRoman(size_t index) {
  const auto first = RomanValue(tokens_[index].text);
  if (first == 0) {
    return std::nullopt;
  }
  std::vector<uint64_t> values = {first};
  const auto dash = IsSymbol(index + 1, "-") || IsSymbol(index + 1, "–") || IsSymbol(index + 1, "—");
  if (
    dash && Glued(index + 1) && IsWord(index + 2) && Glued(index + 2) && RomanValue(tokens_[index + 2].text) > first
  ) {
    values.push_back(RomanValue(tokens_[index + 2].text));
  }
  const auto after = index + 2 * values.size() - 1;
  const auto governed = GovernedBefore(index);
  const auto* century = NounByLemma("век");
  std::optional<NounReading> reading;
  std::string noun;
  auto end = after;
  // "между XII и XIII веками": the noun after the next numeral of the chain.
  auto noun_at = after;
  if (
    IsWord(after) && (tokens_[after].lower == "и" || tokens_[after].lower == "или") && IsWord(after + 1)
    && RomanValue(tokens_[after + 1].text) != 0
  ) {
    noun_at = after + 2;
  }
  if (
    century != nullptr && IsWord(after) && (tokens_[after].text == "в" || tokens_[after].text == "вв")
    && IsSymbol(after + 1, ".") && Glued(after + 1)
  ) {
    // A generated noun: the genitive alone, the prepositional after "в".
    reading = PickReading(
      AnyCase(century->gender, {false}), governed.empty() ? std::vector<GrammaticalCase>{kGen} : governed
    );
    if (std::find(governed.begin(), governed.end(), Case::PREPOSITIONAL) != governed.end()) {
      reading->grammatical_case = Case::PREPOSITIONAL;
    }
    const auto case_index = static_cast<int>(reading->grammatical_case);
    noun = tokens_[after].text == "вв" || values.size() > 1
      ? century->plural[case_index]
      : century->singular[case_index];
    end = SkipAbbreviationDot(after + 1);
  } else if (
    const auto* match = IsWord(noun_at) ? lexicon_.FindNoun(tokens_[noun_at].lower) : nullptr;
    match != nullptr && match->noun->ordinal_use != OrdinalUse::NONE
  ) {
    // "конца XVII века": the singular readings of the noun; "XII и XIII веками": each numeral in the singular.
    auto readings = match->readings;
    const auto singular = std::any_of(readings.begin(), readings.end(), [](const NounReading& noun_reading) {
      return !noun_reading.plural;
    });
    readings.erase(
      std::remove_if(
        readings.begin(),
        readings.end(),
        [singular](const NounReading& noun_reading) {
          return singular && noun_reading.plural;
        }
      ),
      readings.end()
    );
    for (auto& noun_reading : readings) {
      noun_reading.plural = false;
    }
    reading = PickReading(readings, governed);
  } else if (
    (values.size() == 1 || IsOrdinalNoun(index - 1)) && first <= kMaxRomanAfterName && index > 0
    && tokens_[index].space_before && IsWord(index - 1)
    && static_cast<unsigned char>(tokens_[index - 1].text.front()) >= 0x80
    && !lexicon_.IsServiceWord("title", tokens_[index - 1].lower)
    && (StartsUpper(tokens_[index - 1]) || IsOrdinalNoun(index - 1))
  ) {
    auto readings = Guess(tokens_[index - 1].lower);
    if (const auto* name = lexicon_.FindNoun(tokens_[index - 1].lower)) {
      readings = name->readings;
    }
    // "главах II–IV": a range after a plural noun, each numeral in the singular.
    readings.erase(
      std::remove_if(
        readings.begin(),
        readings.end(),
        [&values](const NounReading& noun_reading) {
          return noun_reading.plural && values.size() == 1;
        }
      ),
      readings.end()
    );
    for (auto& noun_reading : readings) {
      noun_reading.plural = false;
    }
    // "увидел Петра II": a verb governs the name, the animate accusative is the genitive.
    auto name_governed = GovernedBefore(index - 1);
    if (name_governed.empty() && StartsUpper(tokens_[index - 1])) {
      name_governed = WordGovernment(index - 1);
    }
    if (lexicon_.FindNoun(tokens_[index - 1].lower) == nullptr && name_governed.empty()) {
      // "Том IV", "Альбом II", "Екатерина II": an unknown name with nothing governing it is a nominative.
      const auto& name = tokens_[index - 1].lower;
      const auto feminine = name.size() >= 2
        && (name.compare(name.size() - 2, 2, "а") == 0 || name.compare(name.size() - 2, 2, "я") == 0);
      reading = NounReading{feminine ? Gender::FEMININE : Gender::MASCULINE, false, kNom};
    } else {
      reading = PickReading(readings, name_governed);
    }
  }
  if (!reading) {
    return std::nullopt;
  }
  if (reading->animate) {
    reading->grammatical_case = kGen;
  }
  Words words;
  for (size_t at = 0; at < values.size(); ++at) {
    // "XX–XXI вв.": the bound of a range is spoken.
    if (at > 0) {
      words.Add("-");
    }
    words.Add(Ordinal(values[at], reading->grammatical_case, reading->gender));
  }
  words.Add(noun);
  Add(words.Text(), PieceKind::WORD, tokens_[index].space_before);
  return end;
}

std::optional<size_t> Normalization::TrySlash(size_t index) {
  if (
    !IsSymbol(index + 1, "/") || !Glued(index + 1) || !IsNumber(index + 2) || !Glued(index + 2)
    || IsSymbol(index + 3, "/")
  ) {
    return std::nullopt;
  }
  const auto& numerator_digits = tokens_[index].text;
  const auto second = ParseAmount(index + 2);
  if (
    !second || numerator_digits.size() > kMaxNumeralDigits || second->digits.size() > kMaxNumeralDigits
    || !second->suffix.empty() || !second->compound.empty() || !second->fraction.empty()
  ) {
    return std::nullopt;
  }
  const auto numerator = ValueOf(numerator_digits);
  const auto denominator = ValueOf(second->digits);
  if (const auto end = TryYearPair(index, numerator, *second)) {
    return end;
  }
  const auto leading_zero = [](const std::string& digits) {
    return digits.size() > 1 && digits.front() == '0';
  };
  // "1/2", "1/1000 м"; an improper one only with a unit or a known noun in the genitive singular ("5/2 л", "5/2 литра"),
  // else "28/2" is "двадцать восемь дробь два".
  constexpr uint64_t max_denominator = 1000;
  Words words;
  const auto measured = !second->units.empty() || [&]() {
    const auto noun = NounAfter(second->end, false);
    return noun.noun != nullptr
      && std::any_of(noun.readings.begin(), noun.readings.end(), [](const NounReading& reading) {
             return !reading.plural && reading.grammatical_case == kGen;
           });
  }();
  if (
    (numerator < denominator || measured) && denominator >= 2 && denominator <= max_denominator
    && !leading_zero(numerator_digits) && !leading_zero(second->digits) && !second->year_abbreviation
  ) {
    auto governed = GovernedBefore(index);
    if (governed.empty()) {
      governed = WordGovernment(index);
    }
    const auto grammatical_case = governed.empty() ? kNom : governed.front();
    const auto one = CountFormOf(numerator) == CountForm::ONE;
    const auto direct = grammatical_case == kNom || grammatical_case == Case::ACCUSATIVE;
    words.Add(Cardinal(numerator, grammatical_case, Gender::FEMININE));
    words.Add(Ordinal(denominator, one || !direct ? grammatical_case : kGen, Gender::FEMININE, !one));
    for (const auto* unit : second->units) {
      words.Add(NounAfterDecimal(*unit->noun));
      words.Add(unit->tail);
    }
    Add(words.Text(), PieceKind::WORD, tokens_[index].space_before);
    return second->end;
  }
  words.Add(ReadGroup(numerator_digits));
  words.Add("др+обь");
  const auto end = AddAmount(words, *second, {});
  Add(words.Text(), PieceKind::WORD, tokens_[index].space_before);
  return end;
}

bool Normalization::IsDistance(const Amount& amount) const {
  // "в 3600 км отсюда", "в 2 км от": a distance after "в"; a written noun tells its case itself ("в 5 минутах").
  return amount.begin > 0 && !amount.units.empty() && IsWord(amount.begin - 1)
    && (tokens_[amount.begin - 1].lower == "в" || tokens_[amount.begin - 1].lower == "во") && IsWord(amount.end)
    && lexicon_.IsServiceWord("distance", tokens_[amount.end].lower);
}

size_t Normalization::EmitAmount(const Amount& amount) {
  // "251 рубль 50 копеек": a pause before the hundredths parts the two amounts.
  if (amount.begin > 0 && IsWord(amount.begin - 1) && amount.units.empty()) {
    const auto* currency = lexicon_.FindNoun(tokens_[amount.begin - 1].lower);
    const auto noun = NounAfter(amount.end, false);
    if (currency != nullptr && currency->noun->subunit != nullptr && noun.noun == currency->noun->subunit) {
      Add(",", PieceKind::CLAUSE, false);
    }
  }
  Words words;
  auto governed = GovernedBefore(amount.begin);
  // "ок. 5 км": the reading of the abbreviation before governs.
  if (
    governed.empty() && amount.begin > 0 && IsSymbol(amount.begin - 1, ".") && tokens_[amount.begin].space_before
    && !pieces_.empty() && pieces_.back().kind == PieceKind::WORD
  ) {
    governed = lexicon_.GovernedCases(LookupKey(pieces_.back().text));
  }
  // A date without a preposition keeps its genitive ("подписали 4 февр."), unless a word of the tables governs it
  // ("датирован 9 мая").
  const auto weak = governed.empty();
  if (weak) {
    governed = NounAfter(amount.end, false).month < 0 ? WordGovernment(amount.begin) : TableGovernment(amount.begin);
  } else if (IsWithCount(amount)) {
    governed = {Case::INSTRUMENTAL, kGen};
  } else if (IsDistance(amount)) {
    governed = {Case::PREPOSITIONAL};
  }
  const auto end = AddAmount(words, amount, governed, weak);
  Add(words.Text(), PieceKind::WORD, tokens_[amount.begin].space_before);
  return end;
}

std::vector<GrammaticalCase> Normalization::WordGovernment(size_t index) const {
  if (index == 0 || !tokens_[index].space_before || !IsWord(index - 1)) {
    return {};
  }
  const auto& word = tokens_[index - 1].lower;
  if (const auto& cases = lexicon_.WordGovernedCases(word); !cases.empty()) {
    return cases;
  }
  // "найди сумму 2 и 3": the next number of a chain, with its noun maybe, takes the cases of the first one; each one
  // is looked at once, so a long chain is linear.
  if ((word == "и" || word == "или") && index >= 2) {
    if (const auto known = word_governed_.find(index); known != word_governed_.end()) {
      return known->second;
    }
    for (size_t back = 2; back <= 4 && back <= index; ++back) {
      if (IsNumber(index - back)) {
        auto cases = WordGovernment(NumberStart(index - back));
        word_governed_[index] = cases;
        return cases;
      }
      if (!IsWord(index - back)) {
        break;
      }
    }
    return {};
  }
  // The dictionary of words knows a transitive verb; without it the end of a verb form.
  const auto forms = word_info_ ? word_info_(word) : std::vector<WordForm>();
  const auto transitive = std::any_of(forms.begin(), forms.end(), [](const WordForm& form) {
    return form.part == PartOfSpeech::VERB && form.transitive;
  });
  if (transitive || (forms.empty() && lexicon_.LooksLikeVerb(word) && lexicon_.FindNoun(word) == nullptr)) {
    return {Case::ACCUSATIVE};
  }
  return {};
}

bool Normalization::IsWithCount(const Amount& amount) const {
  if (
    amount.begin == 0 || !IsWord(amount.begin - 1) || !amount.fraction.empty()
    || NounAfter(amount.end, false).month >= 0
    || (tokens_[amount.begin - 1].lower != "с" && tokens_[amount.begin - 1].lower != "со")
  ) {
    return false;
  }
  if (
    amount.begin >= 2 && IsWord(amount.begin - 2)
    && lexicon_.IsServiceWord("with_from", tokens_[amount.begin - 2].lower)
  ) {
    return false;
  }
  for (auto index = amount.end; index < amount.end + 2 && index < tokens_.size(); ++index) {
    if (IsWord(index) && (tokens_[index].lower == "до" || tokens_[index].lower == "по" || IsDayPart(index))) {
      return false;
    }
  }
  return true;
}

} // namespace giga_edge::vits::normalization
